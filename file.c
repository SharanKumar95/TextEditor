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
#include <fcntl.h>

/***DEFINES***/

#define FILE_VERSION "0.0.1"
#define TAB_STOP 8
#define CNTRL_KEY(k) ((k) & 0x1f)
#define QUIT_TIMES 3

enum editorKey 
{
  BACKSPACE=127,
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

enum editorHighlight 
{
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)
 
/***DATA***/

//Structure definition

struct editorSyntax 
{
  char *filetype;
  char **filematch;
  char *singleline_comment_start;
  char **keywords;
  char *multiline_comment_start;
  char *multiline_comment_end;
  int flags;
};

typedef struct erow 
{
  int idx;
  int size;
  char *chars;
  int rsize;
  char *render;
  unsigned char *hl;
  int hl_open_comment;
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
 int dirty;
 char *filename;
 char statusmsg[80];
 time_t statusmsg_time;
 struct editorSyntax *syntax;
 struct termios orig_termios;
};

struct editorConfig E;

/*** filetypes ***/

char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL };
char *C_HL_keywords[] = {
  "switch", "if", "while", "for", "break", "continue", "return", "else",
  "struct", "union", "typedef", "static", "enum", "class", "case",
  "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
  "void|", NULL
};

struct editorSyntax HLDB[] = {
  {
    "c",
    C_HL_extensions,
    C_HL_keywords,
    "//","/*","*/",
    HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
  },
};

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt, void (*callback)(char *, int));

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
/*Function to get the input from the user one char at a time*/
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

/*Function to get the size of the terminal window*/
int getWindowSize( int *rows , int *cols)
{
 struct winsize ws;
 //TIOCGWINSZ will give the size of the window, but may not work all the time
   if ( ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)             
    {
    //getting window size the hard way
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)        //moving the cursor to the bottom right corner of the screen/window
      {
        return -1;
      }
    return getCursorPosition(rows, cols);                           //get the current position of the cursor
    }
    else 
    {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
    }
}

/*** syntax highlighting ***/

int is_separator(int c) 
{
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void editorUpdateSyntax(erow *row) 
{
  row->hl = realloc(row->hl, row->rsize);
  memset(row->hl, HL_NORMAL, row->rsize);
  if (E.syntax == NULL) 
    return;
  char *scs = E.syntax->singleline_comment_start;
  char *mcs = E.syntax->multiline_comment_start;
  char *mce = E.syntax->multiline_comment_end;
  int scs_len = scs ? strlen(scs) : 0;
  int mcs_len = mcs ? strlen(mcs) : 0;
  int mce_len = mce ? strlen(mce) : 0;
  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);
  int i = 0;

  while (i < row->rsize) 
  {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;
    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, scs_len)) {
        memset(&row->hl[i], HL_COMMENT, row->rsize - i);
        break;
      }
    }
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        } else {
          i++;
          continue;
        }
      } else if (!strncmp(&row->render[i], mcs, mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }
    if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
      if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }
    prev_sep = is_separator(c);
    i++;
  }
  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numRows)
    editorUpdateSyntax(&E.row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) 
{
  switch (hl) 
  {
    case HL_NUMBER: 
        return 31;
    case HL_MATCH:
        return 34;
    case HL_STRING:
        return 35;
    case HL_COMMENT:
    case HL_MLCOMMENT:
        return 36;
    case HL_KEYWORD1: 
        return 33;
    case HL_KEYWORD2: 
        return 32;
    default:
        return 37;
  }
}

void editorSelectSyntaxHighlight() 
{
  E.syntax = NULL;
  if (E.filename == NULL) 
     return;
  char *ext = strrchr(E.filename, '.');
  for (unsigned int j = 0; j < HLDB_ENTRIES; j++) 
  {
    struct editorSyntax *s = &HLDB[j];
    unsigned int i = 0;
    while (s->filematch[i])
     {
      int is_ext = (s->filematch[i][0] == '.');
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) 
      {
        E.syntax = s;
        int filerow;
        for (filerow = 0; filerow < E.numRows; filerow++) 
        {
          editorUpdateSyntax(&E.row[filerow]);
        }
        return;
      }
      i++;
    }
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

int editorRowRxToCx(erow *row, int rx) 
{
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) 
  {
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_STOP - 1) - (cur_rx %TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) 
      return cx;
  }
  return cx;
}

void editorUpdateRow(erow *row)                 //Function to take care of tabs in the file 
{
  int tabs=0;
  int j;
 
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') 
     {
      tabs++;
     }
  free(row->render);
  row->render = malloc(row->size + tabs*(TAB_STOP-1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) 
  {
    if (row->chars[j] == '\t') 
    {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0)
       {
          row->render[idx++] = ' ';
       }
    }
    else
    {
    row->render[idx++] = row->chars[j];
    }
  } 
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

void editorInsertRow(int at, char *s, size_t len) 
{
  if (at < 0 || at > E.numRows) 
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numRows - at));

  for (int j = at + 1; j <= E.numRows; j++) 
       E.row[j].idx++;

  E.row[at].idx = at;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);

  memcpy(E.row[at].chars, s, len);

  E.row[at].chars[len] = '\0';
  E.row[at].rsize=0;
  E.row[at].render=NULL;
  E.row[at].hl=NULL;
  E.row[at].hl_open_comment = 0;

  editorUpdateRow(&E.row[at]);

  E.numRows++;
  E.dirty++;
  
}

void editorFreeRow(erow *row) 
{
  free(row->render);
  free(row->chars);
  free(row->hl);
}

void editorDelRow(int at) {

  if (at < 0 || at >= E.numRows) 
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numRows - at - 1));
  for (int j = at; j < E.numRows - 1; j++) 
    E.row[j].idx--;
  E.numRows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c)                  //Insert characters
{
  if (at < 0 || at > row->size)
     at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) 
{
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at)            //Delete chars
{
 if(at < 0 || at >= row->size)
  {
    return;
  }
 memmove(&row->chars[at], &row->chars[at+1],row->size - at);
 row->size--;
 editorUpdateRow(row);
 E.dirty++;
}

/*** editor operations ***/
void editorInsertChar(int c) 
{
  if (E.cy == E.numRows) 
  {
    editorInsertRow(E.numRows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline() 
{
  if (E.cx == 0) 
  {
    editorInsertRow(E.cy, "", 0);
  }
  else
  {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar()
{
 if(E.cy== E.numRows)
 {
  return;
 }
 if (E.cx == 0 && E.cy == 0) 
   return;
 erow *row = &E.row[E.cy];
 if(E.cx>0)
 {
  editorRowDelChar(row, E.cx-1);
  E.cx--;
  } 
  else 
  {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/*** file i/o ***/


char *editorRowsToString(int *buflen)                           //Function to store the contents of a file in a variable
{   
  int totlen = 0;
  int j;
  //Calculate the total length of the file + 1 for \n on each row
  for (j = 0; j < E.numRows; j++)
  {
    totlen += E.row[j].size + 1;
  }
  *buflen = totlen;
  char *buf = malloc(totlen);
  char *p = buf;
  //Copying the contents of the file into a variable
  for (j = 0; j < E.numRows; j++) 
  {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    //adding \n after every row
    *p = '\n';
    p++;
  }
  return buf;
}

void editorOpen(char *filename)
 {
  free(E.filename);
  E.filename = strdup(filename);

  editorSelectSyntaxHighlight();

  FILE *fp = fopen(filename, "r");
  if (!fp) 
    die("fopen");

  char *line = NULL;                                     // buffer
  size_t linecap = 0;                                    //line capacity: size of line 
  ssize_t linelen;                                       //to store the length of the line
  while ((linelen = getline(&line, &linecap, fp)) != -1)             //to get the length of the line
   {
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
       linelen--;

    editorInsertRow(E.numRows, line,linelen );
   }
  free(line);
  fclose(fp);
  E.dirty=0;
}

void editorSave()                     //Function to save the edited file
{
  if (E.filename == NULL) 
  {
    E.filename = editorPrompt("Save as: %s (ESC to cancel)",NULL);
    if (E.filename == NULL) 
    {
      editorSetStatusMessage("Save aborted");
      return;
    }
    editorSelectSyntaxHighlight();
  }
  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  //ftruncate ensures the file is as big as the buffer, if bigger it truncates the file
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) 
     {
      if (write(fd, buf, len) == len) 
      {
        close(fd);
        free(buf);
        E.dirty=0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/*** find ***/

void editorFindCallback(char *query, int key) 
{
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line;
  static char *saved_hl = NULL;
  if (saved_hl) 
  {
    memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
  }
 
  if (key == '\r' || key == '\x1b') 
  {
    last_match = -1;
    direction = 1;
    return;
  }
  else if (key == ARROW_RIGHT || key == ARROW_DOWN) 
  {
    direction = 1;
  }
  else if (key == ARROW_LEFT || key == ARROW_UP) 
  {
    direction = -1;
  }
  else 
  {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1) 
    direction = 1;
  int current = last_match;
  int i;
  for (i = 0; i < E.numRows; i++) 
  {
    current += direction;
    if (current == -1)
       current = E.numRows - 1;
    else if (current == E.numRows) 
       current = 0;
 
   erow *row = &E.row[current];
    
    char *match = strstr(row->render, query);
    if (match) 
    {
      last_match = current;
      E.cy = current;
      
      E.cx = editorRowRxToCx(row, match - row->render);
      E.rowoff = E.numRows;

      saved_hl_line = current;
      saved_hl = malloc(row->rsize);
      memcpy(saved_hl, row->hl, row->rsize);
      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

void editorFind() 
{
  int saved_cx = E.cx;
  int saved_cy = E.cy;
  int saved_coloff = E.coloff;
  int saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
  if (query) 
  {
    free(query);
  } 
  else 
  {
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
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
      if (len > E.screenCols)
          len = E.screenCols;

      //abAppend(ab, &E.row[filerow].render[E.coloff], len);
      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];
      int current_color = -1;
      int j;
      for (j = 0; j < len; j++) 
      {
        if (iscntrl(c[j])) 
         {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
         }
         if (current_color != -1) 
         {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
         else if (hl[j] == HL_NORMAL) 
         {
         if(current_color!=-1)
         {
          abAppend(ab, "\x1b[39m", 5);
          current_color=-1;
         }
          abAppend(ab, &c[j], 1);
        } 
        else 
        {
          int color = editorSyntaxToColor(hl[j]);
          if (color != current_color) 
          {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
     

    }    
    
    abAppend(ab, "\x1b[K", 3);      

    abAppend(ab, "\r\n",2);

}
}
void editorDrawStatusBar(struct abuf *ab) 
{
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
    E.filename ? E.filename : "[No Name]", E.numRows,
    E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
    E.syntax ? E.syntax->filetype : "no ft", E.cy + 1, E.numRows);
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

//Function to print a status message at the bottom of the screen
void editorSetStatusMessage(const char *fmt, ...)              
{
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}
/***INPUT***/

char *editorPrompt(char *prompt, void (*callback)(char *, int))
{
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';
  while (1) 
  {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    int c = editorReadKey();
    if (c == DEL_KEY || c == CNTRL_KEY('h') || c == BACKSPACE) 
    {
      if (buflen != 0) buf[--buflen] = '\0';
    }
    else if (c == '\x1b') 
    {   
      editorSetStatusMessage("");
      if(callback)
         callback(buf,c);
      free(buf);
      return NULL;
    }
    else if (c == '\r')
    {     
   
      if (buflen != 0)
      {
        editorSetStatusMessage("");
        if(callback)
          callback(buf,c);
        return buf;
      }
    } 
    else if (!iscntrl(c) && c < 128) 
    {
      if (buflen == bufsize - 1) 
      {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }

    if (callback) 
      callback(buf, c);
  }
}

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
 static int quit_times= QUIT_TIMES; 
 switch(c)
 {
   case '\r':
    editorInsertNewline();
    break;

   case CNTRL_KEY('q') :
      if (E.dirty && quit_times > 0) 
       {
        editorSetStatusMessage("WARNING!!! File has unsaved changes. "
          "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
       }
       write(STDOUT_FILENO, "\x1b[2J", 4);
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

  case ARROW_UP: 
  case ARROW_DOWN: 
  case ARROW_LEFT: 
  case ARROW_RIGHT: editorMoveCursor(c);
            break;

  case HOME_KEY:
      E.cx = 0;
      break;
  case END_KEY:
      if (E.cy < E.numRows)
        E.cx = E.row[E.cy].size;
      break;
  case CNTRL_KEY('f'):
      editorFind();
      break;
  case BACKSPACE:
  case CNTRL_KEY('h'):
  case DEL_KEY:
      if (c == DEL_KEY)
         editorMoveCursor(ARROW_RIGHT);
      editorDelChar();
      break;

  case CNTRL_KEY('l'):
  case '\x1b':
      break;
  case CNTRL_KEY('s'):
                      editorSave();
                      break;
  default:
      editorInsertChar(c);
      break;
 }
   quit_times = QUIT_TIMES;
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
 E.dirty=0;
 int i = getWindowSize(&E.screenRows,&E.screenCols);
 if(i==-1)
  {
   die("getWindowSize");
  }
  E.screenRows -=2;
  E.syntax=NULL;
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
  editorSetStatusMessage("HELP: Ctrl+Q = quit | Ctrl+s = Save | Ctrl+f = Find");
  while (1) 
   {
    editorRefreshScreen(); 
    editorProcessKeypress();
   }

  return 0;
}
