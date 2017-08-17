/***INCLUDES**/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

/***DEFINES***/

#define CNTRL_KEY(k) ((k) & 0x1f)
 
/***DATA***/

struct termios orig_termios;

/***TERMINAL***/

void die(const char *s) 
{
  perror(s);
  exit(1);
}

void disableRawMode() 
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) 
  {
    die("tcgetattr");
  }
  atexit(disableRawMode);
  struct termios raw = orig_termios;
  //Unsetting some flags in the terminal to accept raw input
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);    //Input flags
  raw.c_oflag &= ~(OPOST);                                     //Output flags
  raw.c_cflag |= (CS8);                                        //Control flags
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);             //Local flags
  //Adding some timeout for read() 
  raw.c_cc[VMIN] = 0;                               //Min number of bytes required for read() to return (here it is 0)
  raw.c_cc[VTIME] = 1;                              //Time it can wait before returning (in 1/10th of a second) 
 
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
     {
      die("tcsetattr");
     }
}

char editorReadKey()
{
 int bytesRead;
 char c;
 while((bytesRead=read(STDIN_FILENO,&c,1))!=1)
  {
   if(bytesRead==-1 && errno !=EAGAIN )
     {
        die("read");
     }
  }  
  return c;
}

/***OUTPUT***/

void editorRefreshScreen()
{
  write(STDOUT_FILENO, "\x1b[J",4);
}

/***INPUT***/

void editorProcessKeypress()
{
 char c = editorReadKey();
 
 switch(c)
 {
   case CNTRL_KEY('q') : exit(0);
                         break;
 }
}

/***INIT***/

int main() 
{
  enableRawMode();
 
  while (1) 
   {
    editorRefreshScreen(); 
    editorProcessKeypress();
   }

  return 0;
}
