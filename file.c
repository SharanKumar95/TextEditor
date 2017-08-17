/***INCLUDES**/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

/***DEFINES***/

#define CNTRL_KEY(k) ((k) & 0x1f)
 
/***DATA***/

//Structure definition
struct editorConfig
{
 int screenRows;
 int screenCols;
 struct termios orig_termios;
};

struct editorConfig E;

/***TERMINAL***/

void die(const char *s) 
{
  write(STDOUT_FILENO, "\x1b[2J",4);
  write(STDOUT_FILENO, "\x1b[H",3);
  perror(s);
  exit(1);
}

void disableRawMode() 
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) 
  {
    die("tcgetattr");
  }
  atexit(disableRawMode);
  struct termios raw = E.orig_termios;
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

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) 
  {
   return -1;
  }
 // printf("\r\n");
  while (i < sizeof(buf) - 1) 
  {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
    {
     break;
    }
    if (buf[i] == 'R') 
    {
     break;
    }
    i++;
  }
  buf[i] = '\0';
  printf("\r\n&buf[1]: '%s'\r\n", &buf[1]);

  editorReadKey();
  return -1;
}

int getWindowSize( int *rows , int *cols)
{
 struct winsize ws;
   if (1 || ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
    {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      {
        return -1;
      }
    return getCursorPosition(rows, cols);
    }
    else 
    {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
    }
}

/***OUTPUT***/

void editorDrawRows()
{
  int i;
  for(i=0;i<E.screenRows;i++)
  {
    write(STDOUT_FILENO, "~\r\n",3);
  }
}

void editorRefreshScreen()
{
  write(STDOUT_FILENO, "\x1b[2J",4);
  write(STDOUT_FILENO, "\x1b[H",3);
  
  editorDrawRows();
  write(STDOUT_FILENO, "\x1b[H",3);
}

/***INPUT***/

void editorProcessKeypress()
{
 char c = editorReadKey();
 
 switch(c)
 {
   case CNTRL_KEY('q') : write(STDOUT_FILENO, "\x1b[2J", 4);
                         write(STDOUT_FILENO, "\x1b[H", 3);
                         exit(0);
                         break;
 }
}

/***INIT***/

void initEditor()
{
 int i = getWindowSize(&E.screenRows,&E.screenCols);
 if(i==-1)
  {
   die("getWindowSize");
  }
}

int main() 
{
  enableRawMode();
  initEditor();
 
  while (1) 
   {
    editorRefreshScreen(); 
    editorProcessKeypress();
   }

  return 0;
}
