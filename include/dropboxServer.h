#define MAXNAME 64
#define MAXFILES 20
#define FREEDEV -1

struct file_info
{
  char name[MAXNAME];
  char extension[MAXNAME];
  char last_modified[MAXNAME];
  time_t lst_modified;
  int size;
  pthread_mutex_t file_mutex;
};

struct client
{
  int devices[2];
  char userid[MAXNAME];
  struct file_info file_info[MAXFILES];
  int logged_in;
};

struct client_list
{
  struct client client;
  struct client_list *next;
};


struct client_request
{
  char file[200];
  int command;
};

//Estrutura adicionada para enviar contexto do ssl e socket à thread do cliente e sync
typedef struct arg_threads
{
  int socket;
  SSL *ssl;
} arg_threads;


void sync_server(int socket, char *userid);
void receive_file(char *file, SSL *ssl, char*userid); //Modificada, communica-se com SSL
void send_file(char *file, SSL *ssl, char *userid); //Modificada, communica-se com SSL
void send_all_files(SSL *client_ssl, char *userid); //Modificada, communica-se com SSL
int initializeClient(int client_socket, char *userid, struct client *client);
void *client_thread (void *arg_thread);
void *sync_thread_sv(void *arg_thread);
void listen_client(int socket, SSL *client_ssl, char *userid); //Modificada, communica-se com SSL
void initializeClientList();
void send_file_info(SSL *ssl, char *userid); //Modificada, communica-se com SSL
void updateFileInfo(char *userid, struct file_info file_info);
void listen_sync(SSL *client_ssl, char *userid); //Modificada, communica-se com SSL
void close_client_connection(int socket, char *userid);
void delete_file(char *file, SSL *ssl, char *userid); //Modificada, communica-se com SSL
void send_server_time(SSL *client_ssl); //Adicionada para printar hora do servidor