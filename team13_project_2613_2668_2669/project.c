#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __APPLE__
sem_t *mutex, *mutex2, *lock;
#else
sem_t mutex, mutex2, lock;
#endif

#define MAX_DATA_ITEMS 5
#define CACHE_SIZE 5
#define ROUNDS 10
#define DATA_SIZE 5
#define REQ_ROUNDS 5

int max_clients = 10;
int qos = 0;//determines if MRF, EDF or LWT is used
int max=1;//number of request for data item, used in MRF
float min = 1000000;//needed for EDF

// A structure to represent a client request node
struct client_request {
   unsigned int client_num;
   char *req_item;
   char **cached_items;
   int lru;//shows position of cached item lastly used
   int cache_size;
   int deadline;
   clock_t start_t;
   struct client_request *next;
};

struct client{
  unsigned int client_num;
  char cache[CACHE_SIZE][DATA_SIZE];
  clock_t latency;
  struct client *next;
};
struct arg_struct {
    int i;
    char tempReq[DATA_SIZE];
    int deadline;
    struct client_request **clients;
};

struct arg_server{
  struct client_request **requests;
  struct client **clients;
};

struct AdjListNode{
  int client_num;
  int neigbors;
  struct AdjListNode *next;
};

struct AdjList {
  struct AdjListNode *head;
};

struct Graph{
  int V;
  struct AdjList *array;
};

// Function prototypes
int createClient();
struct client_request* newClient_request(struct client_request** array, int client_num, char *req_item, int size, int deadline);
void print(struct client_request **request, struct client **clients);
void maxClique(struct Graph *clients, struct client_request **max_clique, struct client_request **clients_requests);
void fillCache();
int broadcastItems(struct client_request **max_clique);

char *MRI;  // Most Requested Item
char **dataToBroadcast;
int *MRIclients;//[CLIENTS];  //clients array
struct client *client_list = NULL;//head for client list
struct Graph *graph = NULL; 
struct client_request** clients = NULL;

void connect(struct Graph* graph, struct client_request** client, int client_num, char *req, int s);
void removeMaxCliqueEdge(struct Graph* graph, struct client_request **clients);

void* thread(void* arg)
{
    struct arg_struct *args = arg;
    for(int i = 0; i < max_clients; i++){
      createClient();
    }

    for(int g = 0 ; g< REQ_ROUNDS; g++){
      sem_wait(&lock);
      for(int i = 0; i < max_clients; i++){
        /*clients[args[i].i]  = */newClient_request(clients, args[i].i, args[i].tempReq, 5, 4000);
        connect(graph, clients, args[i].i, args[i].tempReq, (args[i].i)+1);
      }
      sem_post(&mutex);
    }
}

int createClient(){
  struct client *p, *prev;
  int i = 0;

  p = client_list;
  prev = p;
  if(p == NULL){
    client_list = (struct client*)malloc(sizeof(struct client)*max_clients);
    //p->cache = NULL;
    client_list->client_num = 0;
    client_list->latency = 0;
    client_list->next = NULL;
  }
  else{
    i++;
    while(p->next != NULL){
      p = p->next;
      prev = p;
      i++;
    }
    p = (struct client*)malloc(sizeof(struct client));
    //p->cache = NULL;
    p->client_num = i;
    p->latency = 0;
    p->next = NULL;
    prev->next = p;
  }
  return i;
}

void *server(void* unused){
    for(int g = 0; g < REQ_ROUNDS; g++){
      sem_wait(&mutex);

      // Calculate priority
      if(qos == 0){
        // MRF
        for(int i=0; i<max_clients; i++){
          char *item = (char*) malloc(DATA_SIZE*sizeof(char)); 
          int counter=0;
          strcpy(item, clients[i]->req_item);
          
          for(int j=0; j<max_clients; j++){
            if(strcmp(item, clients[j]->req_item) == 0){
              counter++;
            }
          }
          if(counter >= max){
            max = counter;
            strcpy(MRI, item);
          }
        }
      }
      else if(qos == 1){
        // EDF
        // add data to client_request node
        char *item = (char*) malloc(DATA_SIZE*sizeof(char));
        int time = 0;
        for(int i=0; i<max_clients; i++){
          // start time - deadline
          time = clients[i]->deadline - (int) clients[i]->start_t;
          strcpy(item, clients[i]->req_item);
          if(time < min){
            min = time;
            strcpy(MRI, item);
          } 
        }
      }
      else if(qos == 2){
        char *item = (char*) malloc(DATA_SIZE*sizeof(char));
        int time = clock();
        int diff = 0;
        for(int i=0; i<max_clients; i++){
          // start time - deadline
          diff = time - (int) clients[i]->start_t;
          strcpy(item, clients[i]->req_item);
          if(diff > max){
            max = diff;
            strcpy(MRI, item);
          } 
        }
      }
      
      struct client_request** max_clique = (struct client_request**) malloc(max_clients*sizeof(struct client_request));//array of clients belonging in max clique
      // print the adjacency list representation of the above graph
      /*if(value>1){//have to zero the mutex, otherwise clients intervene
        sem_wait(&lock);
        while(value > 1){
          sem_getvalue(&mutex2, &value);
          printf("while SEMVALUE(server): %d\n\n" ,value);
          sem_wait(&mutex2);
        }
      }
      sem_wait(&mutex2);*/
      
      print(clients, &client_list);

      maxClique(graph, max_clique, clients);

      printf("\n========= MAX CLIQUE ======== \n\n\n");
      for(int k = 0; max_clique[k]->client_num != -1; k++){
        printf("->%d(%s)",max_clique[k]->client_num, max_clique[k]->req_item);
      }
      
      print(clients, &client_list);
      broadcastItems(max_clique);
      fillCache();

      print(clients, &client_list);

      removeMaxCliqueEdge(graph, clients);
      //free the client for next round 
      sem_post(&lock);
    }
}

void addEdge(struct Graph* graph, struct client_request** clients, int client_num, char *req, int i){
  struct AdjListNode *newNode, *tempNode = NULL;
  int flag = 0;
  if(graph->array[i].head != NULL){ //if not first entry in the list
    
    tempNode = graph->array[i].head;
    while(tempNode->next != NULL){
        tempNode = tempNode->next;
        if(tempNode->client_num == client_num){//check if already a neighbor
          flag = 1;
        }
    }
    if(flag == 0){
      if(graph->array[i].head->neigbors > 0 ){
        graph->array[i].head->neigbors++;
      }
      else{
        graph->array[i].head->neigbors = 1;
      }
      newNode = (struct AdjListNode*)malloc(sizeof(struct AdjListNode));
      newNode->client_num = client_num;
      newNode->next = NULL;
      tempNode->next = newNode;
    }
  }
  else{
    graph->array[i].head = (struct AdjListNode*)malloc(sizeof(struct AdjListNode));
    graph->array[i].head->client_num = i;
    graph->array[i].head->next = (struct AdjListNode*)malloc(sizeof(struct AdjListNode));
    graph->array[i].head->next->client_num = client_num;
    graph->array[i].head->next->next = NULL;
    graph->array[i].head->neigbors = 1;
  }
  
  flag = 0;
  // Since graph is undirected, add an edge from
  // dest to src also
  if(graph->array[client_num].head != NULL){ //if not first entry in the list    
    tempNode = graph->array[client_num].head;
    while(tempNode->next != NULL){
        tempNode = tempNode->next;
        if(tempNode->client_num == i){//check if already a neighbor
          flag = 1;
          break;
        }
    }
    if(flag == 0){
      if(graph->array[client_num].head->neigbors > 0 ){
        graph->array[client_num].head->neigbors++;
      }
      else{
        graph->array[client_num].head->neigbors = 1;
      }
      newNode = (struct AdjListNode*)malloc(sizeof(struct AdjListNode));
      newNode->client_num = i;
      newNode->next = NULL;
      tempNode->next = newNode;
    }
  }
  else{
    graph->array[client_num].head = (struct AdjListNode*)malloc(sizeof(struct AdjListNode));
    graph->array[client_num].head->client_num = client_num;
    graph->array[client_num].head->next = (struct AdjListNode*)malloc(sizeof(struct AdjListNode));
    graph->array[client_num].head->next->client_num = i;
    graph->array[client_num].head->next->next = NULL;
    graph->array[client_num].head->neigbors = 1;
  }
}
void removeEdge(int client_num, int max_node, struct Graph * graph){
  struct AdjListNode* prev, *cur;
  cur = graph->array[client_num].head;
  prev = graph->array[client_num].head;
  graph->array[client_num].head->neigbors -= 1;
  while (cur != NULL)
  {
    if(cur->client_num == max_node){  //delete max_clique node
      prev->next = cur->next;
      
      cur = NULL;
      break;
    }
    prev = cur;
    cur = cur->next;
  }
}
void freeList(struct AdjListNode* client, struct Graph *graph, int j)
{
  struct AdjListNode* tmp, *head;
  if(client != NULL){
    head = client->next;
    while (head != NULL)
    {
        tmp = head;
        head = head->next;
        //if it has no neighbors, just leave it
        if(head == NULL){
        }
        //remove opposite edge as well (undirected graph)
        removeEdge(tmp->client_num, j, graph);
        free(tmp);
    }
    client->next = NULL;
  }
  
}

void removeMaxCliqueEdge(struct Graph* graph, struct client_request **clients){
  //free all max_clique request edges
  for(int j =0 ; j < max_clients; j++){
    if(clients[j]->req_item == NULL)
      freeList(graph->array[clients[j]->client_num].head, graph, j);
  }
  
}

// Calling this function adds all necessary edges to the clients graph.
// s is the total number of clients
void connect(struct Graph* graph, struct client_request** client, int client_num, char *req, int s){
  int exit = 0;
  //check with every other already inputeed client if we can connect the newnode
  for(int i=0; i<s-1; i++){     // s-1 because we don't want to connect client vertex to same client
    // check all client's caches and add edge between them if req is equal
    if(strcmp(client[i]->req_item, req) == 0){  // first case
      addEdge(graph, client, client_num, req, i);
    }

    // Second case
    else{   
      //cache array is a 2d array, or a pointer to an array of 4 elements(for client0)
      //each element constists of 2 chars, so cache array points to a total area of $*2=8 bytes
      //so to find the number of elements, divide by 2
      if((client[i]->cached_items != NULL) || (client[client_num]->cached_items != NULL)){
        int size = client[i]->cache_size; // sizeof(client[i]->cached_items)/2;
        for(int j=0; j<size; j++){

          if(strcmp(client[i]->cached_items[j],req) == 0){
            int size1 = client[client_num]->cache_size;//sizeof(client[client_num]->cached_items)/2;
            for(int z=0; z<size1; z++){
              if(strcmp(client[client_num]->cached_items[z], client[i]->req_item) == 0){
                addEdge(graph, client, client_num, req, i);
                exit = 1;   // Flag
                break;
              }

            }
              if(exit==1){
                  exit = 0;
                  break;
              }
          }
        }
      }
      
    }
  }
}

// A utility function to create a new client_request adjacency list node
struct client_request* newClient_request(struct client_request** array, int client_num, char *req_item, int size, int deadline)
{
  struct client_request* newNode = (struct client_request*) malloc(sizeof(struct client_request));

  // add data to client_request node
  newNode->client_num = client_num;
  newNode->req_item = (char *) malloc(MAX_DATA_ITEMS * sizeof(char));
  newNode->req_item = req_item;
  newNode->deadline = deadline;
  
  // Arriving time of request
  newNode->start_t = clock();
  newNode->lru = 0;
  newNode->cache_size = 0;
  newNode->next = NULL;
  
  array[client_num] = newNode;

  return newNode;
}

void maxClique(struct Graph *clients, struct client_request **max_clique, struct client_request **clients_requests){
  int mri = 0;
  
  // find clients of MRI
  int counter = 0;
  for(int j=0; j<max_clients; j++){
    if(strcmp(MRI, clients_requests[j]->req_item) == 0){
      MRIclients[counter++] = clients_requests[j]->client_num;
    }
  }
  mri = MRIclients[0];
  struct AdjListNode *vertex = clients->array[mri].head;
  
  struct AdjListNode *neighbor = NULL;
  struct AdjListNode *temp = NULL;//the adjlist item of the neighbors list
  int neighbor_neighbor_num = 0;
  int neighbors = 0; 
  counter = 0;//if counter == #of MRIClients neighbors, we add that client to max clique
  int flag = 0;//if 1, then the neighbors neighbor also an MRIlient neigbor, there exist the edges: MRIClient->neighbor and neighbor[i]->neighbor
  
  if(vertex == NULL){//MRIClient has no neighbors, so max_clique only consists of MRIcllients[0](there only exists 1 mriclient, otherwise it would have a neighbor)
    //initialize max_clique array
    for(int y = 0; y <max_clients; y++){
      max_clique[y] = (struct client_request*) malloc(sizeof(struct client_request));
      max_clique[y]->client_num = -1;
    }
    
    max_clique[0]->client_num = mri;  
    max_clique[0]->req_item = clients_requests[mri]->req_item;
    int l = mri;
    struct client *temp = client_list;
    while(l > 0){
      l--;
      temp = temp->next;
    }
    //last step, remove the max_clique requests from the graph
    clients_requests[mri]->req_item = NULL; 
    clock_t end_t = clock();
    temp->latency = end_t - clients_requests[mri]->start_t;

    return;
  }
  else{
    
    neighbors = vertex->neigbors;//num of neighbors of MRIClient, compare it with counter to see if node to be included in max clique
  }
  
  int reject_flag = 0;//if 1, then reject the neigbor from comparing it

  //initialize max_clique array
  for(int y = 0; y <max_clients; y++){
    max_clique[y] = (struct client_request*) malloc(sizeof(struct client_request));
    max_clique[y]->client_num = -1;
  }

  for(int i=0; vertex != NULL; i++){
    counter = 0;
    neighbor = clients->array[vertex->client_num].head;//take the first neighbor of MRIClients adjlist
    //sleep(1); 
    //and loop for each of its own adjlist items
    mri = MRIclients[i];
    for(int j=0; neighbor != NULL; j++){
      neighbor_neighbor_num = neighbor->client_num;
      temp = clients->array[mri].head;
      //sleep(1);
      //check for each of the MRIClients neighbors if they are also neighbors of the current neigbor
      for(int k=0; temp != NULL; k++){
        if((neighbor_neighbor_num == temp->client_num) || (neighbor_neighbor_num == mri)){//we have a matching neighbor
          flag = 1;
          break;
        }

        temp = temp->next;
      }
      if(flag == 1){//match
        counter++;
        flag = 0;
      }
      else{//at least one not common neighbor, the current neighbor is not in max clique, break
      //put current neighbor in reject list (((?????????)))
        reject_flag = 1;
        flag = 0;
        break;
      }

      clock_t end_t = clock();
      //check if current neighbors all connections with MRIClient
      if(counter == neighbors){
        max_clique[i]->client_num = vertex->client_num;   
        max_clique[i]->req_item = clients_requests[vertex->client_num]->req_item;
        int l = vertex->client_num;
        struct client *temp = client_list;
        while(l > 0){
          l--;
          temp = temp->next;
        }
        //last step, remove the max_clique requests from the graph
        clients_requests[vertex->client_num]->req_item = NULL; 
        temp->latency = end_t - clients_requests[vertex->client_num]->start_t;
      }
      neighbor = neighbor->next;
    }
    vertex = vertex->next;
  }
}

void print(struct client_request **request, struct client **clients){
  struct AdjListNode *temp = NULL;
  struct Graph *client = graph;
  for(int i=0; i<client->V; i++){
      printf("Client: %d->", i);
      if(client->array[i].head!=NULL){
        temp = client->array[i].head->next;
      }
      while(temp != NULL){
        printf("%d->", temp->client_num);
        temp = temp->next;
      }
      printf("\n");
  }
  printf("\n\nMOST REQUESTED ITEM: %s ->", MRI);

  for(int i=0; MRIclients[i]!=-1; i++){
    printf("%d, ", MRIclients[i]);
  }
  printf("\n\n\n");
}

//fills cache of clients when items are broadcasted
//uses LRU if a cache already full
void fillCache(int counter){
//for each data item in dataToBroadcast, loop for all clients
//if data already in cache, dont put again
//if data requested, dont put
//otherwise put
  for(int g = 0; g < counter; g++){
    for(int i=0; i<graph->V; i++){
        if(graph->array[i].head!=NULL){
          // insert into cache
          if(clients[i] == NULL){
            printf("ERROR\n\n\n"); 
          }
          //check if cache full
          if(clients[i]->cache_size==CACHE_SIZE){
            //first check if dataToBroadcast[g] already requested OR in cache
            // Check if cache already has Req
            int flag=0;
            for(int z=0; z<clients[i]->cache_size; z++){
              if(strcmp(clients[i]->cached_items[z], dataToBroadcast[g]) == 0){
                flag=1;
                break;
              }
            }
            if(flag ==1){
              continue;
            }
            ////////////
            else{//FIFO CACHE REPLACEMENT
              //check if it is data item is requested
              if(strcmp(clients[i]->req_item, dataToBroadcast[g])!=0){
                strcpy(clients[i]->cached_items[clients[i]->lru], dataToBroadcast[g]);
                strcpy(client_list[i].cache[clients[i]->lru], dataToBroadcast[g]);
                clients[i]->lru++;
                if(clients[i]->lru == CACHE_SIZE){
                  clients[i]->lru = 0;
                }
              }
            }
          }
          else{
            //check if cache empty
            if(clients[i]->cached_items == NULL){
              clients[i]->cached_items = (char **)malloc(sizeof(char *)*CACHE_SIZE*DATA_SIZE);
              clients[i]->cached_items[0] = dataToBroadcast[g];
              //strcpy(clients[i]->cache[0], dataToBroadcast[g]);
              clients[i]->cache_size++;
            }
            else{
              // Check if cache already has Req
              int flag=0;
              for(int z=0; z<clients[i]->cache_size; z++){
                if(strcmp(clients[i]->cached_items[z], dataToBroadcast[g]) == 0){
                  flag=1;
                  break;
                }
              }
              if(flag!=1){
                //check if it is data item is requested
                if(strcmp(clients[i]->req_item, dataToBroadcast[g])!=0){
                  strcpy(clients[i]->cached_items[clients[i]->cache_size], dataToBroadcast[g]);
                  strcpy(client_list[i].cache[clients[i]->cache_size], dataToBroadcast[g]);
                  clients[i]->cache_size++;
                }
              }
            }
          }
        }
    }
  }
}
// A utility function that creates a graph of V vertices
struct Graph* createGraph(int V)
{
    struct Graph* graph = (struct Graph*)malloc(max_clients * sizeof(struct Graph));
    graph->V = V;
    
    // Create an array of adjacency lists.  Size of
    // array will be V
    graph->array = (struct AdjList*)malloc(V * sizeof(struct AdjList));
 
    // Initialize each adjacency list as empty by
    // making head as NULL
    int i;
    for (i = 0; i < V; ++i)
        graph->array[i].head = NULL;
 
    return graph;
}

//find all items to be broadcasted, encoding
int broadcastItems(struct client_request **max_clique){
  int counter = 1; //taken positions of dataToBroadcast array
  int flag = 0;
  dataToBroadcast[0] = (char *)malloc(sizeof(char)*DATA_SIZE);
  dataToBroadcast[0] = max_clique[0]->req_item;
  printf("DATA BROADCASTED: %s %s\n\n\n", dataToBroadcast[0], max_clique[0]->req_item);
  for(int i = 0 ; max_clique[i]->client_num != -1; i++){
    flag = 0;
    //check if item already broadcasted, if not put new item in array to broadcast
    for(int j = 0 ; j < counter; j++){
      if(strcmp(dataToBroadcast[j], max_clique[i]->req_item) == 0){
        flag = 1;
        
        break;
      }
    }
    if(flag == 0){
      printf(" %s ,", max_clique[i]->req_item);
      dataToBroadcast[counter] = (char *)malloc(sizeof(char)*DATA_SIZE);
      dataToBroadcast[counter] = max_clique[i]->req_item;
      counter++; 
    } 
  }  
  printf("DONE\n\n\n");
  return counter;
}

/// ADD VERTEX INSTEAD ADD EDGE /////
int main (int argc, char ** argv){
    int clientsPerRound[10] = { 10, 30, 50, 70, 90, 100, 200, 300, 400, 500};
    if( argc > 1 ) {
      qos = atoi(argv[1]);
    }
    FILE *fp;
    fp = fopen("output.txt", "w+");

    dataToBroadcast = (char**) malloc(clientsPerRound[0]*DATA_SIZE*sizeof(char));
    clients = (struct client_request**) malloc(clientsPerRound[0]*sizeof(struct client_request));
    MRI = (char*) malloc(DATA_SIZE*sizeof(char));
    MRIclients = (int *) malloc(clientsPerRound[0]*sizeof(int));
    struct arg_struct *args = (struct arg_struct*) malloc(clientsPerRound[0]*sizeof(struct arg_struct));
  
    for(int k = 0; k < ROUNDS; k++){
      max_clients = clientsPerRound[k];
      clients = (struct client_request**) realloc(clients, max_clients*sizeof(struct client_request));
      MRIclients = (int *) realloc(MRIclients, max_clients*sizeof(int));
      memset(MRIclients, -1, max_clients*sizeof(int));

      args = (struct arg_struct*) realloc(args, max_clients*sizeof(struct arg_struct));

      graph = createGraph(max_clients);
      
      #ifdef __APPLE__ 
      mutex = sem_open("mutex", O_CREAT, 0644, 0);
      mutex2 = sem_open("mutex2", O_CREAT, 0644, 1);
      lock = sem_open("lock", O_CREAT, 0644, 1);
      #else
      sem_init(&mutex, 0, 0);
      sem_init(&mutex2, 0, 1);
      sem_init(&lock, 0, 1);
      #endif
      pthread_t t;

      printf("GRAPH CREATED\n\n");
      for(int i = 0; i < max_clients; i++){
        // req item
        int reqNum = rand() % MAX_DATA_ITEMS;
        char buffer[10];
        char tempReq[DATA_SIZE];
        buffer[0] = '\0';
        sprintf(buffer, "%d", reqNum);
        strcpy(tempReq, "d");
        strcat(tempReq, buffer);

        args[i].i = i;
        strcpy(args[i].tempReq, tempReq);
      }
      pthread_create(&t,NULL,thread,(void * )args);
      pthread_t s;

      //serv.clients = &client_list;
      pthread_create(&s,NULL,server,NULL);
      //wait for threads to finish
      pthread_join(t,NULL);
      pthread_join(s, NULL);
      #ifdef __APPLE__
      sem_close(mutex);
      sem_close(mutex2);
      #else
      sem_destroy(&mutex);
      sem_destroy(&mutex);
      #endif
      
      //calculate average latency for given number of clients
      clock_t avg_lat = 0, total= 0, sum = 0;
      struct client *temp = client_list;
      for(int i=0; i<max_clients; i++){
        sum += temp->latency; 
        if(temp->latency > 0){
          total++;
        }
        temp = temp->next;
      }
      avg_lat = sum/total;
      printf("Avg latency: %d\n\n",avg_lat);
      fprintf(fp, "%d, %d\n",avg_lat, max_clients);
    }
    fclose(fp);
    return 0;
}