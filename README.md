# Wordle TCP Server

A multithreaded TCP server implementation in C that hosts a Wordle-style word guessing game. The server supports multiple concurrent clients and uses a trie data structure for efficient word validation.

## Features

- **Multithreaded Architecture**: Handles multiple simultaneous game sessions using pthreads
- **TCP Protocol**: Custom binary protocol for client-server communication
- **Trie Data Structure**: Efficient word validation using a prefix tree
- **Thread-Safe Operations**: Mutex-protected shared state for game statistics summaries
- **Graceful Shutdown**: Signal handling for clean server termination
- **Custom Dictionary Support**: Load any word list for gameplay

## Building

Compile all three files together:

```bash
gcc -o wordle_server.out server-main.c server-logic.c -lpthread
gcc -o wordle_client.out wordle-client.c
```

## Usage

### Starting the Server

```bash
./wordle_server <port> <seed> <word-file> <num-words>
```

**Arguments:**

- `port`: TCP port number for the server to listen on
- `seed`: Random seed for word selection
- `word-file`: Path to dictionary file (one 5-letter word per line)
- `num-words`: Number of words to read from the file

**Example:**

```bash
./wordle_server 8192 111 wordle_words.txt 5757
```

### Running the Client

```bash
./wordle_client
```

The client connects to `127.0.0.1:8192` by default. Enter 5-letter words to make guesses.

## Protocol Specification

### Client Request

- Sends 5 ASCII characters (the guessed word)

### Server Response

- **Byte 0**: Status (`'Y'` = valid guess, `'N'` = invalid)
- **Bytes 1-2**: Remaining guesses (unsigned short, network byte order)
- **Bytes 3-7**: Feedback string (5 characters)

**Feedback Format:**

- **Uppercase letter**: Correct letter in correct position
- **Lowercase letter**: Correct letter in wrong position
- **`-`**: Letter not in the word
- **`?????`**: Invalid guess (word not in dictionary)

## Game Rules

- Players have 6 attempts to guess the word
- Only 5-letter words from the loaded dictionary are valid
- Case-insensitive input
- Game ends on correct guess or after 6 attempts

## Implementation Details

### Data Structures

**Trie Node:**

```c
typedef struct Node {
    bool end;              // Marks valid word endings
    struct Node **children; // 26 child pointers (a-z)
} Node;
```

### Thread Safety

Protected shared resources:

- `total_guesses`: Total valid guesses across all games
- `total_wins`: Number of won games
- `total_losses`: Number of lost games
- `words`: Array of words used in each game session

Each uses dedicated mutexes (`guess_mutex`, `result`, `word_mutex`).

### Signal Handling

- **SIGUSR1**: Triggers graceful shutdown
- **SIGINT, SIGTERM, SIGUSR2**: Ignored during operation

## File Structure

- `server-main.c`: Entry point and global variable initialization
- `server-logic.c`: Server implementation (core logic, threading, protocol)
- `wordle-client.c`: Simple command-line client for testing

## Dictionary File Format

Each line must contain exactly 5 lowercase letters followed by a newline in the LF format:

```
apple
beach
crane
dream
...
```

## Limitations

- Maximum 10 queued connections (`MAX_CONNECTION`)
- Fixed 6 guesses per game (`NUM_GUESS`)
- Only supports 5-letter words
- Server must be manually terminated (with `kill -SIGUSR1 <server pid>`)

## Error Handling

The server handles:

- Malformed dictionary files
- Connection failures
- Invalid client inputs
- Thread creation errors
- Premature client disconnections

## Notes

- Each client thread is detached and manages its own game state
- Random word selection uses the provided seed for reproducibility
- The trie is constructed once at startup for all game sessions
- Statistics are maintained globally across all games
