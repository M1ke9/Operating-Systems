#include "tinyos.h"
#include "tinyos.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "util.h"
#include "kernel_dev.h"
#include "kernel_cc.h"
#include "kernel_pipe.h"



socket_cb* PORT_MAP[MAX_PORT+1] = {NULL};



int socket_read(void* socket_cb_t, char* buf, unsigned int n){

	socket_cb* socketcb = (socket_cb*)socket_cb_t;
	if(socketcb->type!=SOCKET_PEER) return -1;
	
	if(socketcb->peer_s.read_pipe!=NULL){
		int read_num = pipe_read(socketcb->peer_s.read_pipe, buf, n);
		return read_num;
	}else
	{
		return -1;
	}
	
}

int socket_write(void* socket_cb_t, const char *buf, unsigned int n){
	
	socket_cb* socketcb = (socket_cb*)socket_cb_t;
	if(socketcb->type!=SOCKET_PEER) return -1;
	if(socketcb->peer_s.write_pipe!=NULL){
		int write_num = pipe_write(socketcb->peer_s.write_pipe, buf, n);
		return write_num;
	}else
	{
		return -1;
	}
	
}

int socket_close(void* socket_cb_t){

	socket_cb* socketcb = (socket_cb*)socket_cb_t;

	if(socketcb->refcount==1){	 //we are sleeping somewhere
		socketcb->refcount--;	//so when we get up to where we are sleeping, refcount will be 0 and socketcb will be freed
		if(socketcb->type == SOCKET_LISTENER){ //means we are sleeping in accept
			PORT_MAP[socketcb->port] = NULL;
				while(!is_rlist_empty(&socketcb->listener_s.queue))
					rlist_pop_front(&socketcb->listener_s.queue);
				kernel_broadcast(&socketcb->listener_s.req_available);	
				return 0;
		}
		else{ 					// we are sleeping in connect
			return 0;
		}
	}
	else if(socketcb->refcount==0){				//we are not sleeping anywhere
			if(socketcb->type==SOCKET_PEER){		//we are just a working peer so set null our pipe ends and free socketcb
					if(socketcb->peer_s.write_pipe != NULL){
						pipe_writer_close(socketcb->peer_s.write_pipe);
						socketcb->peer_s.write_pipe = NULL;
					}
					if(socketcb->peer_s.read_pipe != NULL){
						pipe_reader_close(socketcb->peer_s.read_pipe);	
						socketcb->peer_s.read_pipe = NULL;
					}		
				free(socketcb);
				return 0;
			}
			else if(socketcb->type==SOCKET_LISTENER){//we are just a listener not doing anything. Get out of port_map and free socketcb
				PORT_MAP[socketcb->port] = NULL;
				free(socketcb);
				return 0;
			}
			else if(socketcb->type==SOCKET_UNBOUND){//we are unbounb....free socketcb
				free(socketcb);
				return 0;
			}
		 }
		 return -1;

}

file_ops socket_file_ops = {
  .Open = do_nothing_pt,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close
};


socket_cb* initialize_socket_cb(){
	
	socket_cb* socketcb = (socket_cb*)xmalloc(sizeof(socket_cb));
	
	socketcb->refcount = 0;
	socketcb->fcb = NULL;
	socketcb->type = SOCKET_UNBOUND;
	socketcb->port = NOPORT;

	return socketcb;
}

Fid_t sys_Socket(port_t port)
{

	if(port<0 || port>MAX_PORT) return NOFILE;

	Fid_t fid;
	FCB* fcb;

	socket_cb* socketcb = initialize_socket_cb();

	if(!FCB_reserve(1, &fid, &fcb)) return NOFILE;

	socketcb->fcb = fcb;

	socketcb->fcb->streamfunc = &socket_file_ops;
	socketcb->fcb->streamobj = socketcb;

	socketcb->port = port;
	return fid;
}

int sys_Listen(Fid_t sock)
{

	if(sock<0 || sock>15) return -1;

	FCB* fcb = get_fcb(sock);
	
	if(fcb==NULL || fcb->streamfunc != &socket_file_ops) return -1;

	socket_cb* socketcb = (socket_cb*)fcb->streamobj;

	if(socketcb->type != SOCKET_UNBOUND ||
		socketcb->port == NOPORT ||
			PORT_MAP[socketcb->port]!=NULL) return -1;
	

	PORT_MAP[socketcb->port] = socketcb; //now this port is exclusively for us
	socketcb->type = SOCKET_LISTENER;

	
	//initialize listener_s
	socketcb->listener_s.req_available = COND_INIT;
	rlnode_init(&socketcb->listener_s.queue, NULL);

	return 0;
}


void connect_pipes(socket_cb* request_socket, socket_cb* new_socket){

	request_socket->peer_s.peer = request_socket;
	new_socket->peer_s.peer = new_socket;

	pipe_cb* pipe_one = initialize_pipe_cb();
	pipe_cb* pipe_two = initialize_pipe_cb();

	request_socket->peer_s.read_pipe = pipe_one;
	request_socket->peer_s.write_pipe = pipe_two;

	new_socket->peer_s.read_pipe = pipe_two;
	new_socket->peer_s.write_pipe = pipe_one;

	pipe_one->reader = request_socket->fcb;
	pipe_one->writer = new_socket->fcb;

	pipe_two->reader = new_socket->fcb;
	pipe_two->writer = request_socket->fcb;

	request_socket->type = SOCKET_PEER;
	new_socket->type = SOCKET_PEER;


}

Fid_t sys_Accept(Fid_t lsock)
{
	if(lsock<0 || lsock > 15) {

		return NOFILE;

	}

	FCB* fcb = get_fcb(lsock);

	if(fcb==NULL || fcb->streamfunc != &socket_file_ops) {

		return NOFILE;

	}

	socket_cb* socketcb = (socket_cb*)fcb->streamobj;

	if(socketcb->type!=SOCKET_LISTENER) {

		return NOFILE;

  }
	
	socketcb->refcount++;  

	while(is_rlist_empty(&socketcb->listener_s.queue) && socketcb->refcount==1){
		kernel_wait(&socketcb->listener_s.req_available, SCHED_USER);
	}



	if(socketcb->refcount==0){ 

	   //we were closed while sleeping... make sure to free the space and return NOFILE
		free(socketcb);
		return NOFILE;

	}


	rlnode* queue_node = rlist_pop_front(&socketcb->listener_s.queue);
	connection_request* request = (connection_request*)queue_node->obj;
	socket_cb* request_socket = request->peer;
	
	//create the new socket
	Fid_t new_fid = sys_Socket(socketcb->port);
	if(new_fid==NOFILE){
		kernel_signal(&request->connected_cv);
		return NOFILE;
	}

	FCB* new_fcb = get_fcb(new_fid);
	socket_cb* new_socketcb = (socket_cb*)new_fcb->streamobj;

	connect_pipes(request_socket, new_socketcb);//create and connect the two sockets with pipes.

	request->admitted = 1;
	kernel_signal(&request->connected_cv);
	socketcb->refcount--;
	return new_fid;
	
}


connection_request* initialize_request(socket_cb* socketcb){

	connection_request* request = (connection_request*)xmalloc(sizeof(connection_request));
	request->admitted=0;
	request->connected_cv = COND_INIT;
	request->peer=socketcb;
	rlnode_init(&request->queue_node, request);
	return request;

}

int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	if(sock<0 || sock > 15 ||
		port > MAX_PORT || port <= 0)  return -1;
	
	socket_cb* lsocketcb = PORT_MAP[port];
	if(lsocketcb==NULL ||
		lsocketcb->type!=SOCKET_LISTENER)  return -1;

	FCB* fcb = get_fcb(sock);
	if(fcb==NULL || 
		fcb->streamfunc != &socket_file_ops) return -1;
		
	socket_cb* socketcb = (socket_cb*)fcb->streamobj;

	if(socketcb->type!=SOCKET_UNBOUND)
		return -1;


  
	socketcb->refcount++; 
	

	//create request
	connection_request* request = initialize_request(socketcb);

	//push request in request_queue of listening socket
	rlist_push_back(&lsocketcb->listener_s.queue, &request->queue_node);

	//wakeup accept
	kernel_signal(&lsocketcb->listener_s.req_available);


	//wait untill request is admitted or fails.

	kernel_timedwait(&request->connected_cv, SCHED_USER, timeout);

	int retVal = request->admitted;
	free(request);
	
	if(socketcb->refcount==0){//we've been closed while sleeping
		free(socketcb);
		return retVal-1;
	}else if(socketcb->refcount==1){
		socketcb->refcount--;
		return retVal-1;
	}
   return 0;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	
	if(sock<0 || sock>15) return -1;

	FCB* fcb = get_fcb(sock);
	if(fcb==NULL ||
		fcb->streamfunc != &socket_file_ops) 
		   return -1;

	socket_cb* socketcb = (socket_cb*)fcb->streamobj;
	if(socketcb->type!=SOCKET_PEER) 
		return -1;

	switch(how)
		{
		case SHUTDOWN_READ:	
			if(socketcb->peer_s.read_pipe != NULL){
				pipe_reader_close(socketcb->peer_s.read_pipe);
				socketcb->peer_s.read_pipe = NULL;
			}
				return 0;	
			break;
		case SHUTDOWN_WRITE: 
			if(socketcb->peer_s.write_pipe != NULL){
				pipe_writer_close(socketcb->peer_s.write_pipe);
				socketcb->peer_s.write_pipe = NULL;
			}
				return 0;
			break;
	    case SHUTDOWN_BOTH:
			if(socketcb->peer_s.read_pipe != NULL){
				pipe_reader_close(socketcb->peer_s.read_pipe);
				socketcb->peer_s.read_pipe = NULL;
			}
			if(socketcb->peer_s.write_pipe != NULL){
				pipe_writer_close(socketcb->peer_s.write_pipe);
				socketcb->peer_s.write_pipe = NULL;
			}
				return 0;
			 break;
		default:
			return -1;
			break;

	}
return 0;

}

