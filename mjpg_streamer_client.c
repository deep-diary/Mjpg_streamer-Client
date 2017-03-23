#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>   

#define SERVER_PORT         8080
#define V4L2_PIX_FMT_MJPEG  1
#define BUFFER_SIZE         1024
int iSocketClient;

/* 图片的象素数据 */
typedef struct PixelDatas {
  int iWidth;   /* 宽度: 一行有多少个象素 */
  int iHeight;  /* 高度: 一列有多少个象素 */
  int iBpp;     /* 一个象素用多少位来表示 */
  int iLineBytes;  /* 一行数据有多少字节 */
  int iTotalBytes; /* 所有字节数 */ 
  unsigned char *aucPixelDatas;  /* 象素数据存储的地方 */
}T_PixelDatas, *PT_PixelDatas;

typedef struct VideoBuf {
  T_PixelDatas tPixelDatas;
  int iPixelFormat;
  /* signal fresh frames */
  pthread_mutex_t db;
  pthread_cond_t  db_update;
}T_VideoBuf, *PT_VideoBuf;


typedef struct VideoRecv {
  char *name;
  
  int (*Connect_To_Server)(int *SocketClient, const char *ip);
  int (*DisConnect_To_Server)(int *SocketClient);
  int (*Init)(int *SocketClient);
  int (*GetFormat)(void);
  int (*Get_Video)(int *SocketClient, PT_VideoBuf ptVideoBuf);
  struct VideoRecv *ptNext;
}T_VideoRecv, *PT_VideoRecv;


static int connect_to_server(int *SocketClient, const char *ip);
static int disconnect_to_server(int *SocketClient);
static int init(int *SocketClient);  /* 做一些初始化工作 */
static int getformat(void);
static long int getFileLen(int *SocketClient, char *FreeBuf, int *FreeLen);
static long int http_recv(int *SocketClient, char **lpbuff, long int size);
static int get_video(int *SocketClient, PT_VideoBuf ptVideoBuf);
static void *RecvVideoThread(void *tVideoBuf);


static int connect_to_server(int *SocketClient, const char *ip)
{
  int iRet;
  struct sockaddr_in tSocketServerAddr;
  
  *SocketClient = socket(AF_INET, SOCK_STREAM, 0);
  
  tSocketServerAddr.sin_family      = AF_INET;
  tSocketServerAddr.sin_port        = htons(SERVER_PORT);  /* host to net, short */
  //tSocketServerAddr.sin_addr.s_addr = INADDR_ANY;
  if (0 == inet_aton(ip, &tSocketServerAddr.sin_addr))
  {
    printf("invalid server_ip\n");
    return -1;
  }
  memset(tSocketServerAddr.sin_zero, 0, 8);
  
  iRet = connect(*SocketClient, (const struct sockaddr *)&tSocketServerAddr, sizeof(struct sockaddr));
  if (-1 == iRet)
  {
    printf("connect error!\n");
    return -1;
  }
  
  return 0;
}

static int disconnect_to_server(int *SocketClient)
{
  close(*SocketClient);
  return 0;
}


static int init(int *SocketClient)  /* 做一些初始化工作 */
{
  char ucSendBuf[100];
  int iSendLen;
  
  int iRecvLen;
  unsigned char ucRecvBuf[1000];
  
  /* 发请求类型字符串 */
  memset(ucSendBuf, 0x0, 100);
  strcpy(ucSendBuf, "GET /?action=stream\n");   /* 发送我们是选择 action=stream*/
  iSendLen = send(*SocketClient, ucSendBuf, strlen(ucSendBuf), 0);
  if (iSendLen <= 0)
  {
    close(*SocketClient);
    return -1;
  }
  
  /* 如果我们不使用密码功能!则只需发送任意长度为小于2字节的字符串 */
  memset(ucSendBuf, 0x0, 100);
  strcpy(ucSendBuf, "f\n");   /* 发送我们不选择密码功能 */
  iSendLen = send(*SocketClient, ucSendBuf, strlen(ucSendBuf), 0);
  if (iSendLen <= 0)
  {
    close(*SocketClient);
    return -1;
  }
  
  /* 将从服务器端接收一次报文 */
  /* 接收客户端发来的数据并显示出来 */
  iRecvLen = recv(*SocketClient, ucRecvBuf, 999, 0);
  if (iRecvLen <= 0)
  {
    close(*SocketClient);
    return -1;
  }
  else
  {
    ucRecvBuf[iRecvLen] = '\0';
    printf("http header: %s\n", ucRecvBuf);
  }
  
  return 0;
}



static int getformat(void)
{
  /* 直接返回视频的格式 */
  return V4L2_PIX_FMT_MJPEG;
}


static long int getFileLen(int *SocketClient, char *FreeBuf, int *FreeLen)
{
  int iRecvLen;
  long int videolen;
  char ucRecvBuf[1024];
  char *plen, *buffp;
  
  while(1)
  {
    iRecvLen = recv(*SocketClient, ucRecvBuf, 1024, 0); /* 获取1024字节数据 */
    if (iRecvLen <= 0)
    {
      close(*SocketClient);
      return -1;
    }
    /* sprintf(buffer, "Content-Type: image/jpeg\r\n" \
      *               "Content-Length: %d\r\n" \
        *               "\r\n", frame_size);
    *服务端会发送一些数据报头，这是基本的格式
    *
    */
    /* 解析ucRecvBuf，判断接收到的数据是否是报文 */
    plen = strstr(ucRecvBuf, "Length:");  /* plen指针指向Length字符串开头的地址 */
    if(NULL != plen)
    {
      plen = strchr(plen, ':');  /* 在plen中个找到：出的地址 */
      plen++;                   /* 指向下一个地址，后面的地址存储的是视频数据的真正大小 */
      videolen = atol(plen);    /* 读取视频数据的大小 */
      printf("the Video Len %ld\n", videolen);
    }
    
    buffp = strstr(ucRecvBuf, "\r\n\r\n");   /* \r\n\r\n遇到就跳出while循环 */
    if(buffp != NULL)
      break;
  }   
  
  buffp += 4;   /* 指向真正数据的开头，这个地址在1024字节中 */
  /* 需要注意的是我们已经接受了1024字节的数据，但是这1024字节除了表头后还包含了一部分数据
  *这里我们需要计算一下，这里逻辑关系一定要想明白。
  */
  *FreeLen = 1024 - (buffp - ucRecvBuf);  /* 1024字节中剩余的视频数据 */
  memcpy(FreeBuf, buffp, *FreeLen);     /* 把这部分的视频数据放在FreeBuf缓冲器中 */
  return videolen;     /* 返回1024字节中真正的视频数据的大小 */
}

/* 这个函数用来获取除1024字节（包含数据表头和视频数据）数据以外的视频数据 */
static long int http_recv(int *SocketClient, char **lpbuff, long int size)
{    /* *lpbuff用来存放剩余数据的大小，size剩余视频数据的大小 */
  int iRecvLen = 0, RecvLen = 0;
  char ucRecvBuf[BUFFER_SIZE];
  
  while(size > 0) /* 把这一次传输的剩余数据接收完， */
  {
    iRecvLen = recv(*SocketClient, ucRecvBuf, (size > BUFFER_SIZE)? BUFFER_SIZE: size, 0);
    if (iRecvLen <= 0)
      break;
    
    
    RecvLen += iRecvLen;
    size -= iRecvLen;
    
    
    if(*lpbuff == NULL)
    {
      *lpbuff = (char *)malloc(RecvLen);
      if(*lpbuff == NULL)
        return -1;
    }
    else
    {
      *lpbuff = (char *)realloc(*lpbuff, RecvLen);
      if(*lpbuff == NULL)
        return -1;
    }
    
    memcpy(*lpbuff+RecvLen-iRecvLen, ucRecvBuf, iRecvLen);
  }
  return RecvLen;    /* 返回这次接收到的数据 */
}


/* 这个是真正获取视频所以数据的函数 */
static int get_video(int *SocketClient, PT_VideoBuf ptVideoBuf)
{
  long int video_len, iRecvLen;
  int FirstLen = 0;
  char tmpbuf[1024];
  char *FreeBuffer = NULL;
  
  while(1)   /* 最终数据存放在ptVideoBuf->tPixelDatas.aucPixelDatas这里面了 */
  { 
    /* 接收1024字节中的视频数据 */
    video_len = getFileLen(SocketClient, tmpbuf, &FirstLen);
    /* 接收剩余视频数据 */
    iRecvLen = http_recv(SocketClient, &FreeBuffer, video_len - FirstLen);
    printf("video_len = %ld,iRecvLen = %ld\n", video_len,iRecvLen);
    pthread_mutex_lock(&ptVideoBuf->db);
    
    /* 将两次接收到的视频数据组装成一帧数据 */
    memcpy(ptVideoBuf->tPixelDatas.aucPixelDatas, tmpbuf, FirstLen);
    memcpy(ptVideoBuf->tPixelDatas.aucPixelDatas+FirstLen, FreeBuffer, iRecvLen);
    ptVideoBuf->tPixelDatas.iTotalBytes = video_len;

    pthread_cond_broadcast(&ptVideoBuf->db_update);// 发出一个数据更新的信号，通知发送通道来取数据
    pthread_mutex_unlock( &ptVideoBuf->db );// 原子操作结束
  }
  return 0;
}


/* 构造 */
static T_VideoRecv g_tVideoRecv = {
  .name        = "http",      /* 结构体成员名 */
  .Connect_To_Server  = connect_to_server,     /* 连接到服务器端 */
  .DisConnect_To_Server     = disconnect_to_server,    /* 删除连接 */
  .Init = init,                      /* 做初始化工作 */
  .GetFormat= getformat,                 /* 得到摄像头数据格式 */
  .Get_Video= get_video,                 /* 获取视频数据 */
};


static void *RecvVideoThread(void *tVideoBuf)
{
  g_tVideoRecv.Get_Video(&iSocketClient, (PT_VideoBuf)tVideoBuf);
  return ((void *)0);
}

int main(int argc,char *argv[])
{

  pthread_t RecvVideo_Id;
  PT_VideoBuf ptVideoBuf;
  T_VideoBuf tVideoBuf;
  int handle,res,fileNums=0;
  char fileNames[40]={0};
  time_t nowtime;
  struct tm *timeinfo;
  char *snap_shot_name;

  if (argc != 2)
  {
  printf("Usage:\n");
  printf("%s <ip>\n", argv[0]);
  return -1;
  }
  /* 启动摄像头设备 */ 
  if(g_tVideoRecv.Connect_To_Server(&iSocketClient, argv[1]) < 0)
  { 
    printf("can not Connect_To_Server\n");
    return -1;
  }

  if(g_tVideoRecv.Init(&iSocketClient) < 0)
  {
    printf("can not Init\n");
    return -1;
  }
  //将video_buf清0，用于获取一帧数据
  memset(&tVideoBuf, 0, sizeof(tVideoBuf));
// 分配缓存（30000字节）
  tVideoBuf.tPixelDatas.aucPixelDatas = malloc(100000);

  if( pthread_mutex_init(&tVideoBuf.db, NULL) != 0 )
  /* 初始化 global.db 成员 */
  {
    return -1;
  }
  if( pthread_cond_init(&tVideoBuf.db_update, NULL) != 0 )
  /* 初始化 global.db_update(条件变量) 成员 */
  {
    printf("could not initialize condition variable\n");
    return -1;
  }

  /* 创建获取摄像头数据的线程 */
  pthread_create(&RecvVideo_Id, NULL, &RecvVideoThread, &tVideoBuf);

  if(access("video_snapshot",0)==-1)//access函数是查看文件是不是存在
  {
      if (mkdir("video_snapshot",0777))//如果不存在就用mkdir函数来创建
      {
          printf("creat file bag failed!!!");
          exit(1);
      }
  }
  while(1)
  {

    pthread_cond_wait(&tVideoBuf.db_update, &tVideoBuf.db);
    while(fileNums++<100)
    {
      time(&nowtime);
      timeinfo=localtime(&nowtime);
      //snap_shot_name=strdup(asctime(timeinfo));//这句将结果转变为字符串
      // sprintf(fileNames,"video_snapshot/%d%d%d_%d:%d:%d_%d.jpg",(1900+timeinfo->tm_year),(timeinfo->tm_mon),(timeinfo->tm_mday),(timeinfo->tm_hour),(timeinfo->tm_min),(timeinfo->tm_sec),fileNums);
      // strftime(fileNames, 128, "video_snapshot/%F_%T", timeinfo);
      sprintf(fileNames+strftime(fileNames, 128, "video_snapshot/%F_%T", timeinfo),"_%d.jpg",fileNums);
      printf("%s\n",fileNames);

      if((handle=open(fileNames,O_WRONLY | O_CREAT, 0666))==-1)
      {
        printf("Error opening file.\n");
        exit(1);
      }
      // tVideoBuf.tPixelDatas.aucPixelDatas
      if((res=write(handle,tVideoBuf.tPixelDatas.aucPixelDatas,tVideoBuf.tPixelDatas.iTotalBytes))==-1)
      {
        printf("Error writing to the file.\n");
        printf("%d\n", res);
        exit(1);
      }

      // free(snap_shot_name);
      close(handle);   
      pthread_cond_wait(&tVideoBuf.db_update, &tVideoBuf.db);   

    }



  }

  pthread_detach(RecvVideo_Id);
// 等待线程结束,以便回收它的资源
  return 0;
  
}
