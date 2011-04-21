// author: Madhav P. Desai
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include "Vhpi.h"

#define MAX_CLIENT_SOCKET_COUNT 65536
#define z32__ "00000000000000000000000000000000"

#define APPEND(lst,lnk) if(lst.tail)\
      {\
	lst.tail->next = lnk;\
	lnk->prev = lst.tail;\
	lnk->next = NULL;\
	lst.tail = lnk;\
      }\
    else\
      {\
	lst.head = lnk;\
	lst.tail = lnk;\
	lnk->prev = NULL;\
	lnk->next = NULL;\
      }

#define REMOVE(lst,lnk) if((lst.head == lnk) && (lst.tail == lnk))	\
    {\
      lst.head = NULL;\
      lst.tail = NULL;\
      lnk->next = lnk->prev = NULL;\
    }\
  else if(lst.head == lnk)\
    {\
      lst.head = lnk->next;\
      lnk->next->prev = NULL;\
      lnk->next = lnk->prev = NULL;\
    }\
  else if(lst.tail == lnk)\
    {\
      lst.tail = lnk->prev;\
      lnk->prev->next = NULL;\
      lnk->prev = lnk->next = NULL;\
    }\
  else\
    {\
      lnk->prev->next = lnk->next;\
      lnk->next->prev = lnk->prev;\
      lnk->prev = lnk->next = NULL;\
    }


// maximum id..
int max_socket_id = 0;

// server socket
int server_socket_id = -1;

// client socket array
int client_sockets[MAX_CLIENT_SOCKET_COUNT];
int client_socket_count = 0;

// joblist.
JobList new_jobs;
JobList active_jobs;
JobList finished_jobs;

///////////////////////////////////////////////////////////////////////////////////////////////
// some utility functions.
///////////////////////////////////////////////////////////////////////////////////////////////

// convert 32 character string pointed to by a to an 
// unsigned.
unsigned Bit_Vector_To_Unsigned(char* a)
{
  unsigned ret_val = 0;
  int index;
  if(a != NULL)
    {
      for(index = 31; index >= 0; index--)
	{
	  if(a[index] == 1)	
	    ret_val = ret_val + (1 << (31-index));
	}
    }
  return(ret_val);
}

// pack unsigned x into 32 character string pointed
// to by x.
void Unsigned_To_Bit_Vector(unsigned x, char* a)
{
  int index;
  if(a != NULL)
    {
      for(index = 31; index >= 0; index--)
	{
	  if(x & (1 << (31-index)))
	    a[index] = 1;
	  else
	    a[index] = 0;
	}
    }
}

void Copy_Value(char* dest, int dsize, char* src, int ssize)
{
  if(dsize >= ssize)
    {
      for(int idx = 0; idx < dsize-ssize; idx++)
	{
	  *(dest + idx) = '0';
	}
      for(int idx = 0; idx < ssize; idx++)
	{
	  *(dest + (dsize-ssize) + idx) = *(src+idx);
	}
    }
  else
    {
      for(int idx = (ssize-dsize); idx < ssize; idx++)
       {
	 *(dest + (idx - (ssize - dsize))) = *(src + idx);
       }
    }

  // null termination..
  *(dest + dsize) = 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////
// network-related stuff.
///////////////////////////////////////////////////////////////////////////////////////////////
int Create_Server(int port_number)
{
  int sockfd;
  struct sockaddr_in serv_addr;
  sockfd = socket(PF_LOCAL, SOCK_SEQPACKET, 0);
  fprintf(stderr, "Info: opening socket for server on port %d \n",port_number);

  if (sockfd < 0)
    fprintf(stderr, "Error: in opening socket on port %d\n", port_number);

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(portno);

  if (bind(sockfd, (struct sockaddr *) &serv_addr,
	   sizeof(serv_addr)) < 0)
    {
      fprintf(stderr,"Error: could not bind socket to port %d\n",port_number);
      close(sockfd);
      sockfd= -1;
    }
  else
    fprintf(stderr,"Info: finished binding socket to port %d\n",port_number);
  
  return sockfd;
}

int Connect_To_Client(int server_fd)
{
  int ret_val = 1;
  if(client_socket_count > MAX_CLIENT_SOCKET_COUNT)
    {
      return(0);
    }

  int newsockfd = 0;
  int clilen;
  struct sockaddr_in  cli_addr;
  fd_set c_set;
  struct timeval time_limit;

  time_limit.tv_sec = 0;
  time_limit.tv_usec = 1000;

  FD_SET(server_fd,&c_set);
  int npending = select(max_socket_id + 1, &c_set,NULL,NULL,&time_limit);

  while(npending > 0)
    {
      
      clilen = sizeof(cli_addr);
      newsockfd = accept(server_fd,
			 (struct sockaddr *) &cli_addr,
			 &clilen);
      if (newsockfd > 0)
	{
	  fprintf(stderr,"Info: new client connection %d\n",newsockfd);
	  max_socket_id = (max_socket_id < newsockfd ? newsockfd : max_socket_id);

	  client_sockets[client_socket_count] = newsockfd;
	  client_socket_count++;
	}
      else
	{
	  fprintf(stderr,"Error: failed in accept()\n");	
	  ret_val = 0;
	}

      npending--;
    }

  return(ret_val);
}


int Can_Write_To_Socket(int socket_id)
{
  struct time_limit;
  time_limit.tv_sec = 0;
  time_limit.tv_usec = 1000;
  fd_set c_set;
  FD_ZERO(&c_set);
  FD_SET(socket_id,&c_set);      
  int npending = select(max_socket_id + 1, NULL, &c_set,NULL,&time_limit);

  if(npending > 0)
    return(1);
  else
    return(0);
}


// dang! parsing with strtok again!
void Read_And_Append_To_JobList(int socket_id,JobList* jobs)
{
  char receive_buffer[MAX_BUF_SIZE];

  JobList* new_job = NULL;

  // no need for select.. this socket is ready..
  int n = read(socket_id,receive_buffer,MAX_BUF_SIZE);
  if(n > 0)
    {
      new_job = (JobList*) calloc(1,sizeof(JobList));
      new_job->socket_id = socket_id;

      // format:  "piperead/pipewrite/call  name  num-inp (value)* num-op (width)* 
      char* save_ptr;
      char* type_of_request = strtok_r(receive_buffer," ",&save_ptr);

      // what type of request is it?
      if(strcmp(type_of_request,"piperead") == 0)
	{
	  new_job->is_pipe_read_access = 1;
	}
      else if(strcmp(type_of_request,"pipewrite") == 0)
	{
	  new_job->is_pipe_write_access = 1;
	}
      else if(strcmp(type_of_request,"call") == 0)
	{
	  new_job->is_module_access = 1;
	}
      else
	{
	  fprintf(stderr,"Error: unknown request type : %s\n", type_of_request);
	  return;
	}


      char* name = strtok_r(NULL," ",&save_ptr);
      assert(name != NULL);

      new_job->name = strdup(name);

      // every job has a request and acknowledge port.
      new_job->req_port = (Port*) calloc(1, sizeof(Port));
      new_job->req_port->index = 0;
      new_job->req_port->width = 1;
      new_job->req_port->port_value = (char*) calloc(1,2*sizeof(char));
      sprintf(new_job->req_port->port_value,"1");

      new_job->ack_port = (Port*) calloc(1, sizeof(Port));
      new_job->ack_port->index = 0;
      new_job->ack_port->width = 1;
      new_job->ack_port->port_value = (char*) calloc(1,2*sizeof(char));
      sprintf(new_job->ack_port->port_value,"0");
      

      // the number of inputs?
      char* num_inputs = strtok_r(NULL," ",&save_ptr);
      assert(num_inputs != NULL);
      int ninp = atoi(num_inputs);

      // read each input value..
      for(int idx = 0; idx < ninp; idx++)
	{
	  char* ip_value = strtok(NULL," ",&save_ptr);
	  assert(ip_value != NULL);

	  // each input is  a port.
	  Port* p = (Port*) calloc(1, sizeof(Port));
	  p->index = idx;
	  p->width = strlen(ip_value);
	  p->port_value = strdup(ip_value);

	  APPEND(new_job->inports, p);
	}
      
      // the number of outputs?
      char* num_outputs = strtok_r(NULL," ",&save_ptr);
      assert(num_outputs != NULL);
      int nops = atoi(num_outputs);

      // get the output widths.
      for(int idx = 0; idx < nops; idx++)
	{
	  char* ip_width = strtok_r(NULL," ",&save_ptr);
	  assert(ip_width != NULL);

	  int iw = atoi(ip_width);

	  // new port for each output.
	  Port* p = (Port*) calloc(1, sizeof(Port));
	  p->index = idx;
	  p->width = iw;
	  p->port_value = (char*) calloc(1, (iw+2)*sizeof(char));

	  APPEND(new_jobs->outports, p);
	}

      // append the job.
      APPEND(new_jobs,new_job);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////
// The main Vhpi functions.
///////////////////////////////////////////////////////////////////////////////////////////////
void  Vhpi_Initialize()
{

  new_jobs.head = NULL;
  new_jobs.tail = NULL;
  
  active_jobs.head = NULL;
  active_jobs.tail = NULL;

  finished_jobs.head = NULL;
  finished_jobs.tail = NULL;

  int try_limit = 10;

  // open the socket. and start listening. 
  while(try_limit > 0)
    {

      server_socket_id = Create_Server(9999);
      if(server_socket_id > 0)
	{
	  max_socket_id = server_socket_id;
	  fprintf(stderr,"Info: success: started the server on port 9999\n");
	  break;
	}
      else
	fprintf(stderr,"Info: could not start server on port 9999, will try again\n");

      usleep(1000);

      try_limit--;

      if(try_limit == 0)
	{
	  fprintf(stderr,"Error: tried to start server on port 9999, failed.. giving up\n");
	  exit(-1);
	}
    }

  // ok. listen.. 256 connections should be enough..
  listen(server_socket_id,256);
}

void  Vhpi_Close()
{
  // close all connections and the socket.
}


// go down the list of finished jobs and send
// out the resulting port values..
void  Vhpi_Send()
{
  char send_buffer[MAX_BUF_SIZE];
  char spacer_string[2];
  sprintf(spacer_string, " ");
  JobLink* top, *next;
  PortLink* plink;

  // first look at all active jobs and see if they
  // can be moved to completed status.
  top = active_jobs.head;
  while (top != NULL)
    {
      if(*(top->ack_port->port_value) == '1') 
	{
	  next = top->next;
	  
	  REMOVE(active_jobs,top);
	  APPEND(finished_jobs,top);

	  top = next;
	}
      else
	{
	  top = top->next;
	}
    }

  // look at all completed jobs and send 
  // the data..
  prev = NULL;

  top = finished_jobs.head;
  while(top != NULL)
    {
      if(Can_Write_To_Socket(top->socket_id))
	{
	  if(top->is_pipe_read_access) 
	    {
	      sprintf(send_buffer,"%s",top->outports.head->port->port_value);
	    }
	  else if( top->is_pipe_write_access)
	    {
	      sprintf(send_buffer,"1");
	    }
	  else if(top->is_module_access)
	    {
	      sprintf(send_buffer,"");
	      for(oplink = top->outports.head; oplink != NULL; oplink = oplink->next)
		{
		  send_buffer = strcat(send_buffer,oplink->port->port_value);
		  send_buffer = strcat(send_buffer,spacer_string);
		}
	    }
	  write(top->socket_id,send_buffer,strlen(send_buffer)+1);

	  next = top->next;

	  REMOVE(finished_jobs,top);
	  Delete(top);

	  top = next;
	}
      else
	{
	  top = top->next;
	}
    }
}

void  Vhpi_Listen()
{
  // listen for new connections and
  Connect_To_Client(server_socket_id);

  // now check if any of the clients have anything 
  // worth reading..
  struct time_limit;
  time_limit.tv_sec = 0;
  time_limit.tv_usec = 1000;
  fd_set c_set;
  FD_ZERO(&c_set);
  for(int idx = 0; idx < client_socket_count; idx++)
    {
      FD_SET(client_sockets[idx],&c_set);      
    }
  int npending = select(max_socket_id + 1, &c_set,NULL,NULL,&time_limit);
  if(npending > 0)
    {
      for(int idx = 0; idx < client_socket_count; idx++)
	{
	  if(FD_ISSET(client_sockets[idx],&c_set))
	    {
	      Read_And_Append_To_JobList(client_sockets[idx],new_jobs);
	    }
	}
    }
  FD_ZERO(&c_set);
}


void   Vhpi_Set_Port_Value(char* port_name, char* port_value)
{
  char* save_ptr;
  char* obj_name = strtok_r(port_name," ",&save_ptr);
  char* index_string = strtok_r(NULL," ",&save_ptr);
  assert((obj_name != NULL) && (index_string != NULL));


  // look within jobs that are active.. 
  JobLink* jlink;
  for(jlink = active_jobs.head; jlink != NULL; jlink = jlink->next)
    {
      if(strcmp(jlink->name,obj_name) == 0)
	{
	  if(strcmp(index_string, "ack") == 0)
	    {
	      Copy_Value(jlink->ack_port->value,
			 1,
			 port_value,
			 32);
	    }
	  else
	    {
	      int index = atoi(index_string);
	      PortLink* plink;
	      for(plink = jlink->outports.head; plink != NULL; plink = plink->next)
		{
		  if(index == 0)
		    {
		      Copy_Value(plink->port->port_value,
				 plink->port->width,
				 port_value,
				 32);
		      break;
		    }
		  index--;
		}
	    }
	}
    }
}

void  Vhpi_Get_Port_Value(char* port_name, char* port_value)
{
  char* save_ptr;
  char* obj_name = strtok_r(port_name," ",&save_ptr);
  char* index_string = strtok_r(NULL," ",&save_ptr);
  assert((obj_name != NULL) && (index_string != NULL));
  
  int val_found = 0;
  // look within jobs that are active
  JobLink* jlink = NULL;
  for(jlink = active_jobs.head; jlink != NULL; jlink = jlink->next)
    {
      if(strcmp(jlink->name,obj_name) == 0)
	{
	  break;
	}
    }

  // if not found in active, look in the new jobs.
  // if a new job matches, then move the new job to 
  // the active list.
  if(jlink == NULL)
    {
      for(jlink = new_jobs.head; jlink != NULL; jlink = jlink->next)
	{
	  if(strcmp(jlink->name,obj_name) == 0)
	    {
	      REMOVE(new_jobs,jlink);
	      APPEND(active_jobs,jlink);
	      break;
	    }
	}
    }

  if(jlink != NULL)
    {
      if(strcmp(index_string, "req") == 0)
	{
	  Copy_Value(port_value, 32, jlink->req_port->value, 1);
	  if(jlink->is_module_access)
	    {
	      //
	      // reset the port value.. this makes
	      // it a pulse.
	      //
	      *(jlink->req_port->value) = '0';
	    }
	}
      else
	{
	  int index = atoi(index_string);
	  PortLink* plink;
	  for(plink = jlink->inports.head; plink != NULL; plink = plink->next)
	    {
	      if(index == 0)
		{
		  Copy_Value(port_value,
			     32,
			     plink->port->port_value,
			     plink->port->width);
		  break;
		}
	      index--;
	    }
	}
    }
  else
    {
      // didnt find it, write zero into it..
      sprintf(port_value,z32__);
    }
}



#ifdef GHDL

static void Vhpi_Exit(int sig)
{
  fprintf(stderr, "*** Break! ***\n");
  fprintf(stderr,"Info: Stopping the simulation \n");
  Vhpi_Stop();
  exit(0);
}

int main(int argc, char **argv)
{
  int ret_val;
  signal(SIGINT,  Vhpi_Exit);
  signal(SIGTERM, Vhpi_Exit);

  Vhpi_Start();
  ret_val = (ghdl_main(argc,argv));
  Vhpi_Stop();

  return(ret_val);
}

#endif
