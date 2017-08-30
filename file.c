/***INCLUDES**/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>                             //Making changes to Terminal
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>

/***DEFINES***/

#define FILE_VERSION "0.0.1"
#define TAB_STOP 8
#define CNTRL_KEY(k) ((k) & 0x1f)

enum editorKey 
{
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY
};
 
/***DATA***/

//Structure definition
typedef struct erow 
{
  int size;
  char *chars;
  int rsize;
  char *render;
} erow;

struct editorConfig
{
 int cx,cy;
 int screenRows;
 int screenCols;
 int numRows;
 erow *row;
 int rowoff;
 int coloff;
 int rx;
 char *filename;
 char statusmsg[80];
 time_t statusmsg_time;
 struct termios orig_termios;
};

struct editorConfig E;

/***TERMINAL***/

//Function to exit if there is any failure
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

//Function to disable CANONICAL mode and enable RAW mode
void enableRawMode() 
{
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) 
  {
    die("tcgetattr");
  }
  atexit(disableRawMode);
  struct termios raw = E.orig_termios;
  //Unsetting some flags in the terminal to accept raw input
  /*
  IXON: Disable ctrl+s and ctrl+q
  ICRNL: Disables ctrl+m (ctrl+m is for carriage return)
  */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);    //Input flags

  /*OPOST: Disable any output processing (\n to \r\n processing)*/
  raw.c_oflag &= ~(OPOST);                                     //Output flags
 
  raw.c_cflag |= (CS8);                                        //Control flags

  /*
   ECHO: Print characters you enter ont the screen
   ICANON: To turn off canonical form
   ISIG: To turn off ctrl+c and ctrl+z
   IEXTEN: To turn off ctrl+v
  */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);             //Local flags   
  
  //Adding some timeout for read() 
  raw.c_cc[VMIN] = 0;                               //Min number of bytes required for read() to return (here it is 0)
  raw.c_cc[VTIME] = 1;                              //Time it can wait before returning (in 1/10th of a second) 
 
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
     {
      die("tcsetattr");
     }
}

int  editorReadKey()
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
  if (c == '\x1b')
   {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) 
           return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) 
           return '\x1b';
    if (seq[0] == '[') 
    {
      if (seq[1] >= '0' && seq[1] <= '9')
       {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) 
          return '\x1b';
        if (seq[2] == '~')
          {
           switch (seq[1]) 
           {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
	    case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
           }
        }
      }
      else
       {
        switch (seq[1])
        {
         case 'A': return ARROW_UP;
         case 'B': return ARROW_DOWN;
         case 'C': return ARROW_RIGHT;
         case 'D': return ARROW_LEFT;
         case 'H': return HOME_KEY;
         case 'F': return END_KEY;
       }
      }
    }
    else if (seq[0] == 'O') 
    {
      switch (seq[1])
      {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
   }

    return '\x1b';
  }
  else 
  {  
   return c;
  }
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)               //n- is used to query the terminal 6-is used to get the current cursor position
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

  if (buf[0] != '\x1b' || buf[1] != '[') 
   {
    return -1;
   }
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)        // parsing the output of "\x1b[6n" and storing it in rows and cols variables
  { 
    return -1;
  }
  return 0;
  
}

int getWindowSize( int *rows , int *cols)
{
 struct winsize ws;
   if ( ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) 
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

/***Row OPERATIONS***/

int editorRowCxToRx(erow *row, int cx) 
{
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++)
  {
    if (row->chars[j] == '\t')
      rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

void editorUpdateRow(erow *row) 
{
  int tabs=0;
  int j;
 
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP-1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) 
  {
    if (row->chars[j] == '\t') 
    {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    }
    else
    {
    row->render[idx++] = row->chars[j];
    }
  } 
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) 
{
  E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));               //2D array initialised dynamically
  int at = E.numRows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.row[at].rsize=0;
  E.row[at].render=NULL;
  editorUpdateRow(&E.row[at]);
  E.numRows++;
  
}

/*** file i/o ***/


void editorOpen(char *filename)
 {
  free(E.filename);
  E.filename = strdup(filename);
  FILE *fp = fopen(filename, "r");
  if (!fp) 
    die("fopen");
  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
       linelen--;

  editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) 
{
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL) 
   {
    return;
   }
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) 
{
  free(ab->b);
}

/***OUTPUT***/

void editorScroll() 
{
  E.rx = 0;
  if (E.cy < E.numRows)
  {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }
  if (E.cy < E.rowoff) 
  {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenRows) 
   {
    E.rowoff = E.cy - E.screenRows + 1;
   }
  if (E.rx < E.coloff) 
   {
    E.coloff = E.rx;
   }
  if (E.rx >= E.coloff + E.screenCols) 
   {
    E.coloff = E.rx - E.screenCols + 1;
   }
}

void editorDrawRows(struct abuf *ab)
{
  int i;
  for(i=0;i<E.screenRows;i++)
  {
   int filerow= i+E.rowoff;
   if(filerow>=E.numRows)
   {
    if (E.numRows==0 && i == E.screenRows / 3) 
    {
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome),
        "Sharan Text editor -- version %s", FILE_VERSION);
      if (welcomelen > E.screenCols)
       {
         welcomelen = E.screenCols;
       }
      int padding = (E.screenCols - welcomelen) / 2;
      if (padding) 
      {
        abAppend(ab, "~", 1);
        padding--;
      }
      while (padding--) 
        {
         abAppend(ab, " ", 1);
        }
      abAppend(ab, welcome, welcomelen);
    } 
    else 
    {
      abAppend(ab, "~", 1);
    }
   } 
   else 
    {
      int len = E.row[filerow].rsize-E.coloff;
      if (len<0)
         len =0;
      if (len > E.screenCols) len = E.screenCols;
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }    
    abAppend(ab, "\x1b[K", 3);      

      abAppend(ab, "\r\n",2);

}
}
void editorDrawStatusBar(struct abuf *ab) 
{
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    E.filename ? E.filename : "[No Name]", E.numRows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numRows);
  if (len > E.screenCols) len = E.screenCols;
  abAppend(ab, status, len);
  while (len < E.screenCols) 
  {
    if (E.screenCols - len == rlen) 
    {
      abAppend(ab, rstatus, rlen);
      break;
    } 
    else 
    {
    abAppend(ab, " ", 1);
    len++;
   }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) 
{
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screenCols)
       msglen = E.screenCols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
       abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen()
{
  editorScroll();
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);                              //Hide the cursor
  abAppend(&ab, "\x1b[H", 3);                                 // Reposition the cursor at the top left corner
  
  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buffer[32];
  snprintf(buffer, sizeof(buffer), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,(E.rx - E.coloff) + 1);
  abAppend(&ab, buffer, strlen(buffer));
  abAppend(&ab, "\x1b[?25h", 6);                             //Show the cursor
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) 
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}
/***INPUT***/

void  editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
  switch (key) {
    case ARROW_LEFT:
     if(E.cx!=0)
     {
      E.cx--;
     }
     else if (E.cy > 0)
     {
        E.cy--;
        E.cx = E.row[E.cy].size;
     } 
     break;
    case ARROW_RIGHT:
     if(row && E.cx<row->size)
     {
      E.cx++;
     }
     else if (row && E.cx == row->size) 
     {
        E.cy++;
        E.cx = 0;
     } 
     break;
    case ARROW_UP:
    if(E.cy !=0)
     {
      E.cy--;
     }
      break;
    case ARROW_DOWN:
    if(E.cy!=E.numRows)
    {
      E.cy++;
    }
      break;
  }
  row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) 
  {
    E.cx = rowlen;
  }
}

void editorProcessKeypress()
{
 int  c = editorReadKey();
 
 switch(c)
 {
   case CNTRL_KEY('q') : write(STDOUT_FILENO, "\x1b[2J", 4);
                         write(STDOUT_FILENO, "\x1b[H", 3);
                         exit(0);
                         break;
  
    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) 
        {
          E.cy = E.rowoff;
        }
        else if (c == PAGE_DOWN) 
        {
          E.cy = E.rowoff + E.screenRows - 1;
          if (E.cy > E.numRows) 
             E.cy = E.numRows;
        }
        int times = E.screenRows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
      break;
  case ARROW_UP: editorMoveCursor(c);
  case ARROW_DOWN: editorMoveCursor(c);
  case ARROW_LEFT: editorMoveCursor(c);
  case ARROW_RIGHT: editorMoveCursor(c);
            break;
  case HOME_KEY:
      E.cx = 0;
      break;
  case END_KEY:
      if (E.cy < E.numRows)
        E.cx = E.row[E.cy].size;
      break;
 }
}

/***INIT***/

void initEditor()
{
 E.cx=0;
 E.cy=0;
 E.numRows=0;
 E.row=NULL;
 E.rowoff=0;
 E.coloff=0;
 E.rx=0;
 E.filename=NULL; 
 E.statusmsg[0]='\0';
 E.statusmsg_time=0;

 int i = getWindowSize(&E.screenRows,&E.screenCols);
 if(i==-1)
  {
   die("getWindowSize");
  }
  E.screenRows -=2;
}




//MAIN FUNCTION****************************************************************************************

int main(int argc, char *argv[]) 
{
  enableRawMode();
  initEditor();
  if(argc>=2)
   {
     editorOpen(argv[1]);
   }
  editorSetStatusMessage("HELP: Ctrl-Q = quit");
  while (1) 
   {
    editorRefreshScreen(); 
    editorProcessKeypress();
   }

  return 0;
}
