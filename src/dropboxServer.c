#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "../include/dropboxUtil.h"

#define PORT 4000

struct client_list *client_list;
int writing = 0;

int main()
{
  int serverSockfd, newsockfd, thread;
  socklen_t cliLen;
  struct sockaddr_in serv_addr, cli_addr;
  pthread_t clientThread, syncThread;

  // inicializa lista
  newList(client_list);
  initializeClientList();

  initializeSSL();
  /* SSL Context para Sync */
  SSL_METHOD *method;
  SSL_CTX *ctx; //ponteiro para a estrutura do contexto do sync
  SSL *ssl;


  //Adicionar para função auxiliar
  method = SSLv23_server_method();
  ctx = SSL_CTX_new(method);
  if (ctx == NULL) {
      ERR_print_errors_fp(stderr);
      abort();
  }

  printf("Applying certificates..\n");
  /* SSL carrega certificados */
  if(SSL_CTX_use_certificate_file(ctx, "CertFile.pem", SSL_FILETYPE_PEM) != 1 || SSL_CTX_use_PrivateKey_file(ctx, "KeyFile.pem", SSL_FILETYPE_PEM) != 1) {
    printf("ERROR applying certificates\n");
    exit(0);
  }

  printf("Certificate applied! \n");


  // abre o socket
  if ((serverSockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
  {
    printf("ERROR opening socket\n");
    return -1;
  }
  // inicializa estrutura do serv_addr
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bzero(&(serv_addr.sin_zero), 8);

  // associa o descritor do socket a estrutura
  if (bind(serverSockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
      printf("ERROR on bindining\n");
      return -1;
    }
  // espera pela tentativa de conexão de algum cliente
  listen(serverSockfd, 5);

  cliLen = sizeof(struct sockaddr_in);

  while(1)
  {
    // socket para atender requisição do cliente
    if((newsockfd = accept(serverSockfd, (struct sockaddr *)&cli_addr, &cliLen)) == -1)
    {
      printf("ERROR on accept\n");
      return -1;
    }

    // Adicionando SSL ao socket
     ssl = SSL_new(ctx);
     SSL_set_fd(ssl, newsockfd);
     printf("SETOU ssl ao socket \n");
     int ssl_err = SSL_accept(ssl);
     if(ssl_err <= 0)
     {
         //Erro aconteceu, fecha o SSL

     }


     /* Estrutura criada para armazenar socket de usuário e context de ssl */
     arg_threads *args = malloc(sizeof(arg_threads *));
     args->socket = newsockfd;
     args->ssl = ssl; 

     SSL_read(ssl,	&thread, sizeof(thread));

    if (thread)
    {
      // cria thread para atender o cliente
      if(pthread_create(&clientThread, NULL, client_thread, (void*) args))
      {
        printf("ERROR creating thread\n");
        return -1;
      }
    }
    else
    {
      if(pthread_create(&syncThread, NULL, sync_thread_sv, (void*) args))
      {
        printf("ERROR creating sync thread\n");
        return -1;
      }
    }
  }
}

int initializeClient(int client_socket, char *userid, struct client *client)
{
  struct client_list *client_node;
  struct stat sb;
  int i;

  // não encontrou na lista ---- NEW CLIENT
  if (!findNode(userid, client_list, &client_node))
  {
    client->devices[0] = client_socket;
    client->devices[1] = -1;
    strcpy(client->userid, userid);

    for(i = 0; i < MAXFILES; i++)
    {
      client->file_info[i].size = -1;
    }
    client->logged_in = 1;

    // insere cliente na lista de client
    insertList(&client_list, *client);
  }
  // encontrou CLIENT na lista, atualiza device
  else
  {
    if(client_node->client.devices[0] == FREEDEV)
    {
      client_node->client.devices[0] = client_socket;
    }
    else if (client_node->client.devices[1] == FREEDEV)
    {
      client_node->client.devices[1] = client_socket;
    }
    // caso em que cliente já está conectado em 2 dipostivos
    else
      return -1;
  }

  if (stat(userid, &sb) == 0 && S_ISDIR(sb.st_mode))
  {
    // usuário já tem o diretório com o seu nome
  }
  else
  {
    if (mkdir(userid, 0777) < 0)
    {
      // erro
      if (errno != EEXIST)
        printf("ERROR creating directory\n");
    }
    // diretório não existe
    else
    {
      printf("Creating %s directory...\n", userid);
    }
  }

  return 1;
}

void *sync_thread_sv(void *arg_thread)
{
  arg_threads *arg = (arg_threads *) arg_thread;
  int byteCount, connected;
  int client_socket = arg->socket;
  SSL *ssl = arg->ssl;
  char userid[MAXNAME];
  struct client client;

  // lê os dados de um cliente
  byteCount = SSL_read(ssl, userid, MAXNAME);

  // erro de leitura
  if (byteCount < 1)
    printf("ERROR reading from socket\n");

  listen_sync(ssl, userid);
}

void listen_sync(SSL *client_ssl, char *userid)
{
  int byteCount, command;
  struct client_request clientRequest;

  do
  {
      byteCount = SSL_read(client_ssl, &clientRequest, sizeof(clientRequest));

      switch (clientRequest.command)
      {
        case UPLOAD: receive_file(clientRequest.file, client_ssl, userid); break;
        case DOWNLOADALL: send_all_files(client_ssl, userid); break;
        case DELETE: delete_file(clientRequest.file, client_ssl, userid);
        case EXIT: ;break;
        default: break;
      }
  } while(clientRequest.command != EXIT);
}

void *client_thread (void *arg_thread)
{
  arg_threads *args = (arg_threads *) arg_thread;
  int byteCount, connected;
  //modificado para receber ssl e socket
  int client_socket = args->socket;
  SSL *ssl = args->ssl;
  char userid[MAXNAME];
  struct client client;

  // lê os dados de um cliente
  byteCount = SSL_read(ssl, userid, MAXNAME);

  // erro de leitura
  if (byteCount < 1)
    printf("ERROR reading from socket\n");

  // inicializa estrutura do client
  if (initializeClient(client_socket, userid, &client) > 0)
  {
      // avisamos cliente que conseguiu conectar
      connected = 1;
      byteCount = SSL_write(ssl, &connected, sizeof(int));
      if (byteCount < 0)
        printf("ERROR sending connected message\n");

      printf("%s connected!\n", userid);
  }
  else
  {
    // avisa cliente que não conseguimos conectar

    connected = 0;
    byteCount = SSL_write(ssl, &connected, sizeof(int)); //Modificado envia para ssl
    if (byteCount < 0)
      printf("ERROR sending connected message\n");

    return NULL;
  }

  listen_client(client_socket, ssl, userid);
}

void listen_client(int client_socket, SSL *client_ssl, char *userid)
{
  int byteCount, command;
  struct client_request clientRequest;

  do
  {
      byteCount = SSL_read(client_ssl, &clientRequest, sizeof(clientRequest));

      if (byteCount < 0)
        printf("ERROR listening to the client\n");

      switch (clientRequest.command)
      {
        case LIST: send_file_info(client_ssl, userid); break;
        case DOWNLOAD: send_file(clientRequest.file, client_ssl, userid); break;
        case UPLOAD: receive_file(clientRequest.file, client_ssl, userid); break;
        case TIME: send_server_time(client_ssl);break;
        case EXIT: close_client_connection(client_socket, userid);break;
  //      default: printf("ERROR invalid command\n");
      }
  } while(clientRequest.command != EXIT);
}

void close_client_connection(int socket, char *userid)
{
  struct client_list *client_node;
	int i, fileNum = 0;

  printf("Disconnecting %s\n", userid);

	if (findNode(userid, client_list, &client_node))
	{
    if(client_node->client.devices[0] == FREEDEV)
    {
      client_node->client.devices[1] = FREEDEV;
      client_node->client.logged_in = 0;
    }
    else if (client_node->client.devices[1] == FREEDEV)
    {
      client_node->client.devices[0] = FREEDEV;
      client_node->client.logged_in = 0;
    }
    else if (client_node->client.devices[0] == socket)
      client_node->client.devices[0] = FREEDEV;
    else
      client_node->client.devices[1] = FREEDEV;
  }
}

void send_file_info(SSL *ssl, char *userid)
{
	struct client_list *client_node;
	struct client client;
	int i, fileNum = 0;

	if (findNode(userid, client_list, &client_node))
	{
		client = client_node->client;
		for (i = 0; i < MAXFILES; i++)
		{
			if (client.file_info[i].size != -1)
				fileNum++;
		}

		SSL_write(ssl, &fileNum, sizeof(fileNum));

		for (i = 0; i < MAXFILES; i++)
		{
			if (client.file_info[i].size != -1)
				SSL_write(ssl, &client.file_info[i], sizeof(client.file_info[i]));
		}
	}
}

void delete_file(char *file, SSL *ssl, char *userid)
{
  int byteCount;
  FILE *ptrfile;
  char path[200];
  struct file_info file_info;

  strcpy(path, userid);
  strcat(path, "/");
  strcat(path, file);

  if(remove(path) != 0)
  {
    printf("Error: unable to delete the %s file\n", file);
  }

  strcpy(file_info.name, file);
  file_info.size = -1;

  updateFileInfo(userid, file_info);
}

void receive_file(char *file, SSL *ssl, char*userid)
{
  int byteCount, bytesLeft, fileSize;
  FILE* ptrfile;
  char dataBuffer[KBYTE], path[200];
  struct file_info file_info;
  time_t now;

  strcpy(path, userid);
  strcat(path, "/");
  strcat(path, file);

  if (ptrfile = fopen(path, "wb"))
  {
      // escreve número de bytes do arquivo
      byteCount = SSL_read(ssl, &fileSize, sizeof(fileSize));

      if (fileSize == 0)
      {
        fclose(ptrfile);

      	strcpy(file_info.name, file);
        strcpy(file_info.last_modified, ctime(&now));
        file_info.lst_modified = now;
        file_info.size = fileSize;

      	updateFileInfo(userid, file_info);
        return;
      }

      bytesLeft = fileSize;

      while(bytesLeft > 0)
      {
        	// lê 1kbyte de dados do arquivo do servidor
    		byteCount = SSL_read(ssl, dataBuffer, KBYTE);

    		// escreve no arquivo do cliente os bytes lidos do servidor
    		if(bytesLeft > KBYTE)
    		{
    			byteCount = fwrite(dataBuffer, KBYTE, 1, ptrfile);
    		}
    		else
    		{
    			fwrite(dataBuffer, bytesLeft, 1, ptrfile);
    		}
    		// decrementa os bytes lidos
    		bytesLeft -= KBYTE;
      }
      fclose(ptrfile);

      time (&now);

      strcpy(file_info.name, file);
      strcpy(file_info.last_modified, ctime(&now));
      file_info.lst_modified = now;
      file_info.size = fileSize;

      updateFileInfo(userid, file_info);
  }
}

void updateFileInfo(char *userid, struct file_info file_info)
{
  struct client_list *client_node;
  int i;

  if (findNode(userid, client_list, &client_node))
  {
    for(i = 0; i < MAXFILES; i++)
      if(!strcmp(file_info.name, client_node->client.file_info[i].name))
        {
          client_node->client.file_info[i] = file_info;
          return;
        }
    for(i = 0; i < MAXFILES; i++)
    {
      if(client_node->client.file_info[i].size == -1)
      {
        client_node->client.file_info[i] = file_info;
        break;
      }
    }
  }
}

void send_all_files(SSL *client_ssl, char *userid)
{
  int byteCount, bytesLeft, fileSize, fileNum=0, i;
  FILE* ptrfile;
  char dataBuffer[KBYTE], path[KBYTE];
  struct client_list *client_node;

  if (findNode(userid, client_list, &client_node))
  {
    for(i = 0; i < MAXFILES; i++)
    {
      if (client_node->client.file_info[i].size != -1)
        fileNum++;
    }
  }

  SSL_write(client_ssl, &fileNum, sizeof(fileNum));

  for(i = 0; i < MAXFILES; i++)
  {
    if (client_node->client.file_info[i].size != -1)
    {
      strcpy(path, userid);
      strcat(path, "/");
      strcat(path, client_node->client.file_info[i].name);

      SSL_write(client_ssl, client_node->client.file_info[i].name, MAXNAME);

      if (ptrfile = fopen(path, "rb"))
      {
          fileSize = getFileSize(ptrfile);

          // escreve estrutura do arquivo no servidor
          byteCount = SSL_write(client_ssl, &fileSize, sizeof(int));

          if (fileSize > 0)
          {
            while(!feof(ptrfile))
            {
                fread(dataBuffer, sizeof(dataBuffer), 1, ptrfile);

                byteCount = SSL_write(client_ssl, dataBuffer, KBYTE);
                if(byteCount < 0)
                  printf("ERROR sending file\n");
            }
          }
          fclose(ptrfile);
      }
    }
  }
}

void send_file(char *file, SSL *ssl, char *userid)
{
	int byteCount, bytesLeft, fileSize;
	FILE* ptrfile;
	char dataBuffer[KBYTE], path[KBYTE];

  strcpy(path, userid);
  strcat(path, "/");
  strcat(path, file);

  if (ptrfile = fopen(path, "rb"))
  {
      fileSize = getFileSize(ptrfile);

    	// escreve estrutura do arquivo no servidor
    	byteCount = SSL_write(ssl, &fileSize, sizeof(int));

      while(!feof(ptrfile))
      {
          fread(dataBuffer, sizeof(dataBuffer), 1, ptrfile);

          byteCount = SSL_write(ssl, dataBuffer, KBYTE);
          if(byteCount < 0)
            printf("ERROR sending file\n");
      }
      fclose(ptrfile);
  }
  // arquivo não existe
  else
  {
    fileSize = -1;
    byteCount = SSL_write(ssl, &fileSize, sizeof(fileSize));
  }
}

void initializeClientList()
{
  struct client client;
  DIR *d, *userDir;
  struct dirent *dir, *userDirent;
  int i = 0;
  FILE *file_d;
  struct stat st;
  char folder[MAXNAME], path[200];

  d = opendir(".");
  if (d)
  {
    while ((dir = readdir(d)) != NULL)
    {
      if (dir->d_type == DT_DIR && strcmp(dir->d_name,".")!=0 && strcmp(dir->d_name,"..")!=0)
       {
          client.devices[0] = FREEDEV;
          client.devices[1] = FREEDEV;
          client.logged_in  = 0;

          strcpy(client.userid, dir->d_name);

          userDir = opendir(dir->d_name);

          strcpy(folder, dir->d_name);
          strcat(folder, "/");

          if (userDir)
          {
            for(i = 0; i < MAXFILES; i++)
            {
              client.file_info[i].size = -1;
              if (pthread_mutex_init(&client.file_info[i].file_mutex, NULL) != 0)
              {
                  printf("\n mutex init failed\n");
                  return 1;
              }
            }
            i = 0;
            while((userDirent = readdir(userDir)) != NULL)
            {
              if(userDirent->d_type == DT_REG && strcmp(userDirent->d_name,".")!=0 && strcmp(userDirent->d_name,"..")!=0)
              {
                 strcpy(path, folder);
                 strcat(path, userDirent->d_name);

                 stat(path, &st);

                 strcpy(client.file_info[i].name, userDirent->d_name);

                 client.file_info[i].size = st.st_size;

                 client.file_info[i].lst_modified = st.st_mtime;

                 strcpy(client.file_info[i].last_modified, ctime(&st.st_mtime));

                 i++;
              }
            }
            insertList(&client_list, client);
          }
       }
    }

    closedir(d);
  }
}

void send_server_time(SSL *client_ssl)
{
    time_t request_time, now;
    int byteCount;

    // hora da requisição
    printf("Requested time\n");
    request_time = time(&now);
    byteCount = SSL_write(client_ssl, &request_time, sizeof(request_time));
    if(byteCount < 0)
      printf("ERROR sending file\n");

}