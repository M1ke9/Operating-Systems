#include "tinyos.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "util.h"
#include "kernel_cc.h"
#include "kernel_sched.h"


/****************************
 
 The pipe driver

 ****************************/
//NEWWWW

int do_nothing(){
  return -1;
}

	void* do_nothing_pt(){
		return NULL;
	}

int pipe_write(void* pipecb_t, const char *buf, unsigned int n){

  pipe_cb* pipecb = (pipe_cb*)pipecb_t;
  int i;
  
  if(pipecb->reader==NULL)
    return -1;

  while(pipecb->empty_slots == 0)
    kernel_wait(&pipecb->has_space, SCHED_USER);

 for(i=0; i<n; i++){

  pipecb->BUFFER[pipecb->w_position] = buf[i];
  
  if(pipecb->w_position == PIPE_BUFFER_SIZE-1)
	  pipecb->w_position=0;
  else
    pipecb->w_position++;

  pipecb->empty_slots = pipecb->empty_slots - 1;
    
  if(pipecb->empty_slots == 0){
    kernel_broadcast(&pipecb->has_data);
	  return i+1;
  }

 }
 
 kernel_broadcast(&pipecb->has_data);
 return i;
 
}

int pipe_read(void* pipecb_t, char* buf, unsigned int n){

  pipe_cb* pipecb = (pipe_cb*)pipecb_t;
  int i;


if(pipecb->empty_slots==PIPE_BUFFER_SIZE && pipecb->writer==NULL)
  return 0;


while(pipecb->empty_slots==PIPE_BUFFER_SIZE)
	kernel_wait(&pipecb->has_data, SCHED_USER);

  for(i=0; i<n; i++){
     
    buf[i] = pipecb->BUFFER[pipecb->r_position];
    
	if(pipecb->r_position==PIPE_BUFFER_SIZE-1){
		pipecb->r_position=0;
  }
	else
		pipecb->r_position++;
	
	  pipecb->empty_slots = pipecb->empty_slots + 1;

	if(pipecb->empty_slots==PIPE_BUFFER_SIZE){
		kernel_broadcast(&pipecb->has_space);
		return i+1;
	}

  }
  
  kernel_broadcast(&pipecb->has_space);
  return i;


}

int pipe_writer_close(void* _pipecb){


pipe_cb* pipecb = (pipe_cb*)_pipecb;
 if(pipecb->writer == NULL && pipecb->reader == NULL)
   return 0;

pipecb->writer = NULL;
if(pipecb->reader==NULL){
 free(pipecb);
 pipecb = NULL;

}
return 0;

}

int pipe_reader_close(void* _pipecb){


pipe_cb* pipecb = (pipe_cb*)_pipecb;
 if(pipecb->writer == NULL && pipecb->reader == NULL)
   return 0;


pipecb->reader = NULL;
if(pipecb->writer==NULL){
 free(pipecb);
 pipecb = NULL;
}

return 0;

}


file_ops reader_file_ops = {
  .Open = do_nothing_pt,
  .Read = pipe_read,
  .Write = do_nothing,
  .Close = pipe_reader_close
};


file_ops writer_file_ops = {
  .Open = do_nothing_pt,
  .Read = do_nothing,
  .Write = pipe_write,
  .Close = pipe_writer_close
};




pipe_cb* initialize_pipe_cb(){
 
  pipe_cb* pipecb =(pipe_cb*)xmalloc(sizeof(pipe_cb)); 
  pipecb->reader = NULL;
  pipecb->writer = NULL;
  pipecb->has_data = COND_INIT;
  pipecb->has_space = COND_INIT;
  pipecb->w_position = 0;
  pipecb->r_position = 0;
  pipecb->empty_slots = PIPE_BUFFER_SIZE;
  return pipecb;
}


int sys_Pipe(pipe_t* pipe){

  Fid_t fid[2];
  FCB* fcb[2];
  
  pipe_cb* pipecb = initialize_pipe_cb();
  
  if(!FCB_reserve(2, fid, fcb))
    return -1;

  pipe->read = fid[0];
  pipe->write = fid[1];

  pipecb->reader = fcb[0];
  pipecb->writer = fcb[1];

  pipecb->reader->streamfunc = &reader_file_ops;
  pipecb->writer->streamfunc = &writer_file_ops;

  pipecb->reader->streamobj = pipecb;
  pipecb->writer->streamobj = pipecb;
  
  return 0;

}
	


