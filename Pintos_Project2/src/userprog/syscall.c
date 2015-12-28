
/*All of the newly added or modified functions,implemtations are finished by Lai
 *ZhengMin,Jin Xin and Jiang LinXi,Explanations are accomplished by
 * Lai ZhengMin*/
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

//*Added by Lai ZhengMin,Jin Xin and Jiang LinXi*//
#include "threads/vaddr.h"
#include "threads/init.h"
#include "userprog/process.h"
#include <list.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include "threads/synch.h"

static void syscall_handler (struct intr_frame *);

//* Added by Jiang LinXi*//
typedef int pid_t;

/*Added by Lai ZhengMin,Jin Xin and Jiang LinXi*/

//define those functions for the syscall
//int sys_exit (int status);
static int sys_write (int fd, const void *buffer, unsigned length);
static int sys_halt (void);
static int sys_create (const char *file, unsigned initial_size);
static int sys_open (const char *file);
static int sys_close (int fd);
static int sys_read (int fd, void *buffer, unsigned size);
static int sys_exec (const char *cmd);
static int sys_wait (pid_t pid);
static int sys_filesize (int fd);
static int sys_tell (int fd);
static int sys_seek (int fd, unsigned pos);
static int sys_remove (const char *file);

//these functions are auxiliary for implementing syscall
static struct file *find_file_by_fd (int fd);
static struct fd_elem *find_fd_elem_by_fd (int fd);
static int alloc_fid (void);
static struct fd_elem *find_fd_elem_by_fd_in_process (int fd);

/*define the type of the system call handler，the address is provided by three pointers */
typedef int (*handler) (uint32_t, uint32_t, uint32_t);
static handler syscall_vec[128];// the array for storing the syscall handler.
static struct lock file_lock;//lock for accessing the file

// define a struct fd_elem to associate the thread with file and fd.
struct fd_elem
  {
    int fd;
    struct file *file;
    struct list_elem elem;
    struct list_elem thread_elem;
  };
  
static struct list file_list;/*list to store all the open files */

/*Implemented by Lai ZhengMin ,Jin Xin and Jiang LinXi*/
void
syscall_init (void) /* Initialize the syscall，called by main() */
{
    /* make the syscall_handler become the interrupt vector 0x30
     the number '3' in parameters list represents user can call this interrupt */
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  /* use syscall_vec[] to map the syscall value to the relevant function.
   the syscall number is defined in syscall-nr.h */
  syscall_vec[SYS_EXIT] = (handler)sys_exit;
  syscall_vec[SYS_HALT] = (handler)sys_halt;
  syscall_vec[SYS_CREATE] = (handler)sys_create;
  syscall_vec[SYS_OPEN] = (handler)sys_open;
  syscall_vec[SYS_CLOSE] = (handler)sys_close;
  syscall_vec[SYS_READ] = (handler)sys_read;
  syscall_vec[SYS_WRITE] = (handler)sys_write;
  syscall_vec[SYS_EXEC] = (handler)sys_exec;
  syscall_vec[SYS_WAIT] = (handler)sys_wait;
  syscall_vec[SYS_FILESIZE] = (handler)sys_filesize;
  syscall_vec[SYS_SEEK] = (handler)sys_seek;
  syscall_vec[SYS_TELL] = (handler)sys_tell;
  syscall_vec[SYS_REMOVE] = (handler)sys_remove;
  
  list_init (&file_list);
  lock_init (&file_lock);
}

static void
syscall_handler (struct intr_frame *f)//execute program according to the syscall number
{
  handler h;
  int *p;
  int ret;
  
  p = f->esp;/* acquire the address where the syscall number is stored */
  
  if (!is_user_vaddr (p))/*can't access the kernel address */
    goto terminate;
  
  if (*p < SYS_HALT || *p > SYS_INUMBER)/*exceed the range of syscall value*/
    goto terminate;
  
  h = syscall_vec[*p];/*fetch the nedded function to h from syscall_vec */
  
  /* the next parameter must be in the virtual memory*/
  if (!(is_user_vaddr (p + 1) && is_user_vaddr (p + 2) && is_user_vaddr (p + 3)))
    goto terminate;
  
  ret = h (*(p + 1), *(p + 2), *(p + 3));/* execute the correspondent function */
  
  f->eax = ret;/* store the return value in f->eax */
  
  return;
  
terminate:
  sys_exit (-1);

}

/* write bytes of length from buffer to file related to fd */
static int
sys_write (int fd, const void *buffer, unsigned length)
{
  struct file * f;
  int ret;
  
  ret = -1;
  lock_acquire (&file_lock);
  /*If it is stdout,use putbuf */
  if (fd == STDOUT_FILENO) 
    putbuf (buffer, length);
  else if (fd == STDIN_FILENO) /* if it is stdin ,return error */
    goto done;
  /* if the buffer exceed the range of user virtual memory , terminate the user program*/
  else if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + length))
    {
      lock_release (&file_lock);
      sys_exit (-1);
    }
  else/* If it is legal */
    {
      f = find_file_by_fd (fd);/* find the file by fd */
      if (!f)
        goto done;
        
      ret = file_write (f, buffer, length);/*write into f*/
    }
    
done:
  lock_release (&file_lock);
  return ret;/* return the real written bytes */
}

/* exit the current user program */
int
sys_exit (int status)
{
  struct thread *t;
  struct list_elem *l;
  
  t = thread_current ();
  while (!list_empty (&t->files))/* close all the files of that user programs opened. */
    {
      l = list_begin (&t->files);
      sys_close (list_entry (l, struct fd_elem, thread_elem)->fd);
    }
  
  t->ret_status = status;
  thread_exit ();
  return -1;
}

/* close pintos */
static int
sys_halt (void)
{
  power_off ();
}

/* create a file with assigned size*/
static int
sys_create (const char *file, unsigned initial_size)
{
  if (!file)
    return sys_exit (-1);
  return filesys_create (file, initial_size);
}

/*Open a file */
static int
sys_open (const char *file)
{
  struct file *f;
  struct fd_elem *fde;
  int ret;
  
  ret = -1; /*initialization*/
  if (!file) /*if the file doesn't exist,retun -1*/
    return -1;
  if (!is_user_vaddr (file))/* if it is not user program,terminate*/
    sys_exit (-1);
  f = filesys_open (file);
  if (!f) /*Fail to open the file */
    goto done;
    
  fde = (struct fd_elem *)malloc (sizeof (struct fd_elem));
  if (!fde) /* if there is no enough space to create fd_elem for the new opened file */
    {
      file_close (f);
      goto done;
    }
    
  fde->file = f;
  fde->fd = alloc_fid ();/* allocate a fd frame */
  /* store the file into file_list */
  list_push_back (&file_list, &fde->elem);
  list_push_back (&thread_current ()->files, &fde->thread_elem);
  ret = fde->fd;
done:
  return ret;
}

/*close the correspondent file of fd*/
static int
sys_close(int fd)
{
  struct fd_elem *f;
  int ret;
  
  f = find_fd_elem_by_fd_in_process (fd);
  
  if (!f)
    goto done;
   /* close the file,remove it from list_file */
  file_close (f->file);
  list_remove (&f->elem);
  list_remove (&f->thread_elem);
  free (f);/*release the correspondent struct fd_elem */
  
done:
  return 0;
}

/* read size bytes from the fd file to buffer*/
static int
sys_read (int fd, void *buffer, unsigned size)
{
  struct file * f;
  unsigned i;
  int ret;
  
  ret = -1;
  lock_acquire (&file_lock);
  /* If it is STDIN */
  if (fd == STDIN_FILENO) 
    {
      for (i = 0; i != size; ++i)
        *(uint8_t *)(buffer + i) = input_getc ();
      ret = size;
      goto done;
    }
  else if (fd == STDOUT_FILENO) /* IF it is STDOUT,return error */
      goto done;
      /*if the buffer exceeds the range of virtual memory ,terminate the program*/
  else if(!is_user_vaddr(buffer)||!is_user_vaddr(buffer+size))
    {
      lock_release (&file_lock);
      sys_exit (-1);
    }
  else
    {
      f = find_file_by_fd (fd);
      if (!f)
        goto done;
      ret = file_read (f, buffer, size);/* read file */
    }
    
done:    
  lock_release (&file_lock);
  return ret;
}

/*execute the commond*/
static int
sys_exec (const char *cmd)
{
  int ret;
  
  /* If the command is null or access the memory illegally,return */
  if (!cmd || !is_user_vaddr (cmd)) 
    return -1;
  lock_acquire (&file_lock);
  ret = process_execute (cmd);/*use process_execute to excute the command*/
  lock_release (&file_lock);
  return ret;
}

/* wait the pid process to die */
static int
sys_wait (pid_t pid)
{
  return process_wait (pid);
}

/* return the bytes of the file */
static int
sys_filesize (int fd)
{
  struct file *f;
  
  f = find_file_by_fd (fd);
  if (!f)
    return -1;
  return file_length (f);
}

/* return  the location of the next operation*/
static int
sys_tell (int fd)
{
  struct file *f;
  
  f = find_file_by_fd (fd);
  if (!f)
    return -1;
  return file_tell (f);
}

/* set the next position that will be oprated with pos.*/
static int
sys_seek (int fd, unsigned pos)
{
  struct file *f;
  
  f = find_file_by_fd (fd);
  if (!f)
    return -1;
  file_seek (f, pos);
  return 0;
}

/* delete the file*/
static int
sys_remove (const char *file)
{
  if (!file)
    return false;
  if (!is_user_vaddr (file))
    sys_exit (-1);
    
  return filesys_remove (file);
}


/* auxiliary functions */
static struct file *
find_file_by_fd (int fd)
{
  struct fd_elem *ret;
  
  ret = find_fd_elem_by_fd (fd);
  if (!ret)
    return NULL;
  return ret->file;
}

static struct fd_elem *
find_fd_elem_by_fd (int fd)
{
  struct fd_elem *ret;
  struct list_elem *l;
  
  for (l = list_begin (&file_list); 
       l != list_end (&file_list); 
       l = list_next (l))
    {
      ret = list_entry (l, struct fd_elem, elem);
      if (ret->fd == fd)
        return ret;
    }
    
  return NULL;
}

static struct fd_elem *
find_fd_elem_by_fd_in_process (int fd)
{
  struct fd_elem *ret;
  struct list_elem *l;
  struct thread *t;
  
  t = thread_current ();
  
  for (l = list_begin (&t->files); l != list_end (&t->files); l = list_next (l))
    {
      ret = list_entry (l, struct fd_elem, thread_elem);
      if (ret->fd == fd)
        return ret;
    }
  return NULL;
}

static int
alloc_fid (void)
{
  static int fid = 2;/* we remain 0,1 for the STDIN and STDOUT */
  return fid++;
}
/* auciliary function */

/*All of the newly added or modified functions,implemtations are finished by Lai
 *ZhengMin,Jin Xin and Jiang LinXi,Explanations are accomplished by
 * Lai ZhengMin*/
