#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

/**
 *
 * Test with
 * ./wordle_server 8192 111 wordle_words.txt 5757
 *
 */

// Simple Server Configuration
#define MAX_CONNECTION 10
#define NUM_GUESS 6

extern int total_guesses;
extern int total_wins;
extern int total_losses;
extern char **words;
pthread_mutex_t guess_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t result = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t word_mutex = PTHREAD_MUTEX_INITIALIZER;

int numGame;
int serverPort;
int randomSeed;
char *wordFile;
int numWord;
char **allWords;

struct sockaddr_in tcp_server;
int listener;
bool running = true;
typedef struct Node
{
  bool end;
  struct Node **children;
} Node;
Node *root = NULL;

// Setting up
int extractArgument(int argc, char **argv);
int readWordFile();
int setupServer();
void setSignalResponse();
void createTrie();

// Server Function
int mainServer();
void *childServer();

// Clean
void sighandler(int signum);
void cleanTrie();
void cleanPath(Node *current);

// Utilities
void logging(const char *);
void reply(int, int, char *);
bool checkValidWord(char *);
bool determineReply(const char *answer, const char *guess, char *reply);
void createPath();
Node *createNode();

int wordle_server(int argc, char **argv)
{
  if (extractArgument(argc, argv) == -1)
  {
    fprintf(stderr, "ERROR: Invalid argument(s)\n");
    fprintf(stderr, "USAGE: wordle_server <listener-port> <seed> <word-filename> <num-words>");
    return EXIT_FAILURE;
  }
  if (readWordFile() == -1)
    return EXIT_FAILURE;
  if (setupServer() == -1)
    return EXIT_FAILURE;
  setSignalResponse();
  createTrie();
  srand(randomSeed);

  mainServer();
  return EXIT_SUCCESS;
}

// Setting up
int extractArgument(int argc, char **argv)
{
  if (argc != 5)
    return -1;

  char **runArgv = argv;
  serverPort = atoi(*(++runArgv));
  randomSeed = atoi(*(++runArgv));
  wordFile = *(++runArgv);
  numWord = atoi(*(++runArgv));
  numGame = 0;

  if (serverPort == 0)
    return -1;
  if (randomSeed == 0)
    return -1;
  if (numWord == 0)
    return -1;
  if (*(++runArgv) != NULL)
    return -1;
  return 0;
}

int readWordFile()
{
  int fd = open(wordFile, O_RDONLY);
  if (fd == -1)
  {
    perror("open() failed");
    return -1;
  }
  // Realloc words
  allWords = calloc(numWord + 1, sizeof(char *));

  for (int i = 0; i < numWord; i++)
  {
    *(allWords + i) = calloc(6, sizeof(char));
    int rc = read(fd, *(allWords + i), 6);
    if (rc != 6 || *(*(allWords + i) + 5) != '\n')
    {
      fprintf(stderr, "Error word: %s, last char = '%c'\n", *(allWords + i), *(*(allWords + i) + 5));
      fprintf(stderr, "ERROR: Malformed word");
      return -1;
    }
    *(*(allWords + i) + 5) = '\0';

    // printf("READ: %s\n", words[i]);
  }

  printf("MAIN: opened %s (%d words)\n", wordFile, numWord);

  close(fd);
  return EXIT_SUCCESS;
}

int setupServer()
{
  listener = socket(AF_INET, SOCK_STREAM, 0);

  if (listener == -1)
  {
    perror("socket() failed");
    return -1;
  }

  // For IPv4
  tcp_server.sin_family = AF_INET;
  // Allowing any IP to connect
  tcp_server.sin_addr.s_addr = htonl(INADDR_ANY);
  // Set port for the server
  tcp_server.sin_port = htons(serverPort);

  if (bind(listener, (struct sockaddr *)&tcp_server, (socklen_t)sizeof(tcp_server)) == -1)
  {
    perror("bind() failed");
    return -1;
  }

  if (listen(listener, MAX_CONNECTION) == -1)
  {
    perror("listen() failed");
    return -1;
  }

  printf("MAIN: Wordle server listening on port {%d}\n", serverPort);
  return 0;
}

void setSignalResponse()
{
  signal(SIGINT, SIG_IGN);
  signal(SIGTERM, SIG_IGN);
  signal(SIGUSR2, SIG_IGN);
  signal(SIGUSR1, sighandler);
}

void createTrie()
{
  root = createNode();
  for (char **ptr = allWords; *ptr; ptr++)
    createPath(*ptr);
}

// Server Function

int mainServer()
{
  while (running)
  {
    struct sockaddr_in remote_client;
    int addrlen = sizeof(remote_client);

    int *newsd = calloc(1, sizeof(int));
    *newsd = accept(listener, (struct sockaddr *)&remote_client, (socklen_t *)&addrlen);

    if (*newsd == -1)
    {
      free(newsd);
      if (running == false)
        break;
      perror("accept() failed");
      continue;
    }

    printf("MAIN: rcvd incoming connection request\n");

    pthread_t tid;
    int rc = pthread_create(&tid, NULL, childServer, newsd);
    if (rc != 0)
    {
      free(newsd);
      fprintf(stderr, "ERROR: thread_create() failed");
      continue;
    }
  }
  printf("MAIN: Wordle server shutting down...\n");
  return 0;
}

void *childServer(void *sd)
{
  char *guessBuffer = calloc(10, sizeof(char));
  char *secondBuffer = calloc(5, sizeof(char));

  char *guessDiff = calloc(10, sizeof(char));
  char *log = calloc(50, sizeof(char));
  char *answer = *(allWords + (rand() % numWord));
  unsigned short guessLeft = NUM_GUESS;
  int serverDescriptor = *((int *)sd);

  pthread_mutex_lock(&word_mutex);
  {
    words = realloc(words, (++numGame + 1) * sizeof(char *));
    *(words + (numGame - 1)) = calloc(6, sizeof(char));
    strcpy(*(words + (numGame - 1)), answer);
    for (int i = 0; i < 5; i++)
      *(*(words + (numGame - 1)) + i) = toupper(*(*(words + (numGame - 1)) + i));
  }
  pthread_mutex_unlock(&word_mutex);

  // Detach thread
  pthread_t tid = pthread_self();
  pthread_detach(tid);

  while (1)
  {
    logging("waiting for guess");
    // Blocking on receive
    int n = recv(serverDescriptor, guessBuffer, 5, 0);
    *(guessBuffer + n) = '\0';
    // Wait until get all 5
    int m;
    while (n != 5)
    {
      m = recv(serverDescriptor, secondBuffer, 1, 0);
      strncat(guessBuffer, secondBuffer, 1);
      if (m == -1)
      {
        perror("recv() failed");
        return NULL;
      }
      else if (m == 0)
      {
        n = 0;
        break;
      }
      n++;
    }

    if (n == -1)
    {
      perror("recv() failed");
      return NULL;
    }
    else if (n == 0)
    {
      logging("client gave up; closing TCP connection...");
      pthread_mutex_lock(&result);
      {
        total_losses++;
      }
      pthread_mutex_unlock(&result);
      for (int i = 0; i < 5; i++)
        *(guessDiff + i) = toupper(*(answer + i));
      sprintf(log, "game over; word was %s!", guessDiff);
      logging(log);
      break;
    }
    else
    {
      sprintf(log, "rcvd guess: %s", guessBuffer);
      logging(log);

      for (char *c = guessBuffer; *c; c++)
        *c = tolower(*c);
      for (char *c = guessDiff; *c; c++)
        *c = '?';

      // Determine reply
      bool valid = checkValidWord(guessBuffer);
      if (!valid)
      {
        //  invalid guess
        if (guessLeft == 1)
          sprintf(log, "invalid guess; sending reply: %s (%hu guess left)", guessDiff, guessLeft);
        else
          sprintf(log, "invalid guess; sending reply: %s (%hu guesses left)", guessDiff, guessLeft);
        logging(log);
        reply(serverDescriptor, guessLeft, "?????");
      }
      else
      {
        --guessLeft;
        pthread_mutex_lock(&guess_mutex);
        {
          total_guesses++;
        }
        pthread_mutex_unlock(&guess_mutex);
        // logging("Valid guess");
        bool correct = determineReply(answer, guessBuffer, guessDiff);
        if (guessLeft == 1)
          sprintf(log, "sending reply: %s (%hu guess left)", guessDiff, guessLeft);
        else
          sprintf(log, "sending reply: %s (%hu guesses left)", guessDiff, guessLeft);
        logging(log);
        reply(serverDescriptor, guessLeft, guessDiff);
        if (n == -1)
        {
          perror("send() failed");
          break;
        }

        if (correct || guessLeft == 0)
        {
          // game over; word was READY!
          if (!correct)
            for (int i = 0; i < 5; i++)
              *(guessDiff + i) = toupper(*(answer + i));
          sprintf(log, "game over; word was %s!", guessDiff);
          logging(log);
          pthread_mutex_lock(&result);
          {
            if (correct)
              total_wins++;
            else
              total_losses++;
          }
          pthread_mutex_unlock(&result);
          break;
        }
      }
    }
  }

  free(guessBuffer);
  free(secondBuffer);

  free(guessDiff);
  free(log);
  free((int *)sd);
  close(serverDescriptor);
  return NULL;
}

// Clean up
void sighandler(int signum)
{
  // Close listener
  close(listener);
  cleanTrie();

  for (char **ptr = allWords; *ptr; ptr++)
    free(*ptr);
  free(allWords);
  running = false;
  printf("MAIN: SIGUSR1 rcvd; Wordle server shutting down...\n");
}

void cleanTrie()
{
  cleanPath(root);
}

void cleanPath(Node *current)
{
  for (int i = 0; i < 26; i++)
  {
    if (*(current->children + i))
    {
      cleanPath(*(current->children + i));
    }
  }
  free(current->children);
  free(current);
}

// Utilities
void logging(const char *str)
{
  printf("THREAD %lu: ", pthread_self());
  printf("%s\n", str);
}

void reply(int sd, int guessLeft, char *reply)
{
  char *r = calloc(9, sizeof(char));
  if (*reply == '?')
    *r = 'N';
  else
    *r = 'Y';
  (*(short *)(r + 1)) = htons(guessLeft);
  for (int i = 0; i < 5; i++)
    *(r + i + 3) = *(reply + i);
  *(r + 8) = '\0';
  send(sd, r, sizeof(r), 0);
  free(r);
}

bool checkValidWord(char *word)
{
  Node *current = root;
  for (char *c = word; *c; c++)
  {
    int index = (*c) - 'a';
    if (*(current->children + index) == NULL)
      return false;
    current = *(current->children + index);
  }
  return true;
}

bool determineReply(const char *answer, const char *guess, char *reply)
{
  for (int i = 0; i < 5; i++)
    *(reply + i) = '-';

  bool correct = true;
  for (int i = 0; i < 5; i++)
  {
    if (*(guess + i) == *(answer + i))
      *(reply + i) = toupper(*(guess + i));
    else
    {
      correct = false;
      for (int j = 0; j < 5; j++)
      {
        if (*(guess + i) == *(answer + j))
          *(reply + i) = tolower(*(guess + i));
      }
    }
  }
  return correct;
}

void createPath(char *word)
{
  Node *current = root;
  for (char *c = word; *c; c++)
  {
    int index = (*c) - 'a';
    if (*(current->children + index) == NULL)
      *(current->children + index) = createNode();
    current = *(current->children + index);
  }
  current->end = true;
}

Node *createNode()
{
  Node *newNode = calloc(1, sizeof(Node));
  newNode->end = false;
  newNode->children = calloc(26, sizeof(Node *));
  return newNode;
}
