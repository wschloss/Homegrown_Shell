#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <readline/readline.h>
#include <readline/history.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "builtins.h"

// Potentially useful #includes (either here or in builtins.h):
//   #include <dirent.h>
//   #include <errno.h>
//   #include <fcntl.h>
//   #include <signal.h>
//   #include <sys/errno.h>
//   #include <sys/param.h>
//   #include <sys/types.h>
//   #include <sys/wait.h>
//   #include <unistd.h>

using namespace std;


// The characters that readline will use to delimit words
const char* const WORD_DELIMITERS = " \t\n\"\\'`@><=;|&{(";

// An external reference to the execution environment
extern char** environ;

// Define 'command' as a type for built-in commands
typedef int (*command)(vector<string>&);

// A mapping of internal commands to their corresponding functions
map<string, command> builtins; 
// Variables local to the shell
map<string, string> localvars;

// Currently assigned aliases
map<string, string> aliases;


// Handles external commands, redirects, and pipes.
int execute_external_command(vector<string> tokens) {
  // Get the program name
  string progname = tokens[0];
  // convert args into a char** structure for the exec call
  char** argv = new char*[tokens.size() + 1]; // need a null at the end
  for (int i = 0; i < tokens.size(); i++) {
    argv[i] = new char[tokens[i].size() + 1];
    strcpy(argv[i], tokens[i].c_str());
  }
  argv[tokens.size()] = NULL;
  // Fork and execute the command in the child
  int cpid;
  if ((cpid = fork()) == -1) {
    perror("fork");
    return -1;
  }
  if (cpid == 0) {
    //child, call the exec syscall
    execvp(progname.c_str(), argv);
    // if we get here, there was an error
    perror("execve");
    exit(1);
    return -1;
  } else {
    //parent, wait for the child to finish
    int status;
    if (wait(&status) == -1) {
      perror("wait");
      return -1;
    }
    return status;
  }
}


// Return a string representing the prompt to display to the user. It needs to
// include the current working directory and should also use the return value to
// indicate the result (success or failure) of the last command.
string get_prompt(int return_value) {
  if (return_value != 0) {
      return pwd() + " :( $ ";
  }
  return pwd() + " :) $ ";
}


// Return one of the matches, or NULL if there are no more.
char* pop_match(vector<string>& matches) {
  if (matches.size() > 0) {
    const char* match = matches.back().c_str();

    // Delete the last element
    matches.pop_back();

    // We need to return a copy, because readline deallocates when done
    char* copy = (char*) malloc(strlen(match) + 1);
    strcpy(copy, match);

    return copy;
  }

  // No more matches
  return NULL;
}


// Generates environment variables for readline completion. This function will
// be called multiple times by readline and will return a single cstring each
// time.
char* environment_completion_generator(const char* text, int state) {
  // A list of all the matches;
  // Must be static because this function is called repeatedly
  static vector<string> matches;

  // If this is the first time called, construct the matches list with
  // all possible matches
  if (state == 0) {
    // get the name of the var (no $)
    string vartext = text;
    vartext = vartext.substr(1);
    // iterate over environ vars, push valid options
    int count = 0;
    while (environ[count]) {
      if (vartext[0] == environ[count][0]) {
        // Get the environment up to the =
        string push_this = environ[count];
        push_this = push_this.substr(0,push_this.find("="));
        matches.push_back(string("$") + push_this);
      }
      count ++;
    }
    // iterate over the rest of the text, get rid of invalid matches
    count = 2; // start at the second char ($12....)
    while (matches.size() > 0 && text[count]) {
      for (int i = 0; i < matches.size(); i++) {
        if (matches[i][count] != text[count]) {
          matches.erase(matches.begin() + i);
          i--;
        }
      }
      count++;
    }

  }

  // Return a single match (one for each time the function is called)
  return pop_match(matches);
}


// Fills the passed vector with programs in the $PATH variable
void getPathPrograms(vector<string>& matches) {
  // Get the path string
  string path;
  for (int i = 0; environ[i]; i++) {
    path = environ[i];
    if (path.substr(0,4) == "PATH") { 
      break;
    }
  }
  // Check we found it
  if (path.substr(0,4) != "PATH") return;
  // Split up the path into the directories
  path = path.substr(5);
  vector<string> dirStrings;
  size_t i;
  while ((i = path.find(":")) != string::npos) {
    dirStrings.push_back(path.substr(0,i));
    path = path.substr(i+1);
  }
  // For each directory, add all progs to matches
  for (int i = 0; i < dirStrings.size(); i++) {
    DIR* dir = opendir(dirStrings[i].c_str());
    // dont continue if there was an error
    if (!dir) continue;
    // Iterate over progs in the directory
    for (dirent* cur = readdir(dir); cur; cur = readdir(dir)) {
      // Dont push . or ..
      if ((string)cur->d_name != ".." && (string)cur->d_name != ".")
        matches.push_back(cur->d_name);
    }
    closedir(dir);
  }  
}


// Generates commands for readline completion. This function will be called
// multiple times by readline and will return a single cstring each time.
char* command_completion_generator(const char* text, int state) {
  // A list of all the matches;
  // Must be static because this function is called repeatedly
  static vector<string> matches;

  // If this is the first time called, construct the matches list with
  // all possible matches
  if (state == 0) {
    // Fill with all $PATH progs and commands
    // Commands...
    typedef map<string, command>::iterator it;
    for (it i = builtins.begin(); i != builtins.end(); i++) {
      matches.push_back(i->first); 
    }
    // PATH progs...
    getPathPrograms(matches);

    // Check for a tab with no text
    if (text[0] == '\0') {
      return pop_match(matches);
    }

    // Iterate over the text, remove all invalid matches
    int testpos = 0;
    while (text[testpos] && matches.size() > 0) {
      for (int i = 0; i < matches.size(); i++) {
        if (matches[i][testpos] != text[testpos]) {
          matches.erase(matches.begin() + i);
          i--; //adjust for the shifting of the vector
        } 
      }
      testpos++;
    }
  }

  // Return a single match (one for each time the function is called)
  return pop_match(matches);
}


// This is the function we registered as rl_attempted_completion_function. It
// attempts to complete with a command, variable name, or filename.
char** word_completion(const char* text, int start, int end) {
  char** matches = NULL;

  if (start == 0) {
    rl_completion_append_character = ' ';
    matches = rl_completion_matches(text, command_completion_generator);
  } else if (text[0] == '$') {
    rl_completion_append_character = ' ';
    matches = rl_completion_matches(text, environment_completion_generator);
  } else {
    rl_completion_append_character = '\0';
    // We get directory matches for free (thanks, readline!)
  }

  return matches;
}


// Transform a C-style string into a C++ vector of string tokens, delimited by
// whitespace.
vector<string> tokenize(const char* line) {
  vector<string> tokens;
  string token;

  // istringstream allows us to treat the string like a stream
  istringstream token_stream(line);

  while (token_stream >> token) {
    tokens.push_back(token);
  }

  // Search for quotation marks, which are explicitly disallowed
  for (size_t i = 0; i < tokens.size(); i++) {

    if (tokens[i].find_first_of("\"'`") != string::npos) {
      cerr << "\", ', and ` characters are not allowed." << endl;

      tokens.clear();
    }
  }

  return tokens;
}


// Splits the vector of tokens by pipe characters.
// Returns of new token vectors without the pipe characters
// If the location of pipes is not valid, returns an empty structure
vector< vector<string> > splitByPipes(vector<string> tokens) {
  // Vector of split portions
  vector< vector<string> > ret;
  // search for pipes until all processed
  while(true) {
    int pipepos;
    // Search for a pipe pos
    for ( pipepos = 0; pipepos < tokens.size(); pipepos++ ) {
      if (tokens[pipepos] == "|")
        break;
    }

    // Check for invalid pipe positions
    if (pipepos == 0 || pipepos == tokens.size() - 1) {
      // Clear out to signal an error
      ret.clear();
      return ret;
    } 
    // if we didn't find another pipe
    else if (pipepos == tokens.size())
      break;

    // Position is good, split off the tokens up to the pipe
    vector<string> command;
    for ( int i = 0; i < pipepos; i++ ) {
      command.push_back(tokens[0]);
      tokens.erase(tokens.begin());
    }
    // Erase the pipe char
    tokens.erase(tokens.begin());

    // Add the split off command
    ret.push_back(command);
  } 
  // add remaining tokens as last command
  ret.push_back(tokens);
  return ret;
}


// Searches for a redirection token, if found, splits off token and file from vector
// Sets up the file descriptors appropriately
// If no token found, returns a -2, if an error occurs, returns -1
int redirectionScan(vector<string>& tokens) {
  // Scan from the back end to get the last char
  int i;
  for (i = tokens.size() - 1; i >= 0; i--) {
    if (tokens.at(i) == ">" || tokens.at(i) == "<" || tokens.at(i) == ">>")
      break;
  }
  // If didn't find one
  if (i == -1) return -2;
  // Ensure the position of the redirect token makes sense
  if (i == 0 || i != tokens.size() - 2) {
    cerr << "Invalid file redirection\n";
    return -1;
  } 

  // Set up file descriptor appropriately
  string redirect = tokens[i];
  string filestring = tokens[i + 1]; 
  // Erase both tokens
  tokens.erase(tokens.begin() + i);
  tokens.erase(tokens.begin() + i); 
  
  int descriptor;
  // Sets file permissions
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;
  if (redirect == "<") {
    // Check to make sure not used with a pipe since that doesn't make sense
    if (splitByPipes(tokens).size() > 1) {
      cerr << "Invalid combination of pipes and file redirection\n";
      return -1;
    }
    descriptor = open(filestring.c_str(), O_RDONLY, mode);
  }
  else if (redirect == ">")
    descriptor = open(filestring.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode); 
  else if (redirect == ">>")
    descriptor = open(filestring.c_str(), O_WRONLY | O_APPEND | O_CREAT, mode);

  // Check for error
  if (descriptor == -1) { 
    perror("open error");
    return -1;
  }

  // Swap out the file descriptor for STDIN or STDOUT
  if (redirect == ">" || redirect == ">>") {
    dup2(descriptor, STDOUT_FILENO);
  }
  else if (redirect == "<") {
    dup2(descriptor, STDIN_FILENO);
  }

  // close it
  close(descriptor); 
  return descriptor;
}


// Executes the command to write to a pipe.  Returns the file descriptor for
// the read end so it can be fed to another command.
// Returns -1 if there was an error.
int piped_execution(vector<string>& tokens) {
  // Declare pipe
  int the_pipe[2];
  int cpid;

  // open up the pipe
  if (pipe(the_pipe) == -1) {
    perror("pipe");
    return -1;
  }
  
  // Fork to execute in the child process
  if ((cpid = fork()) == -1) {
    perror("fork");
    // close pipe for good measure
    close(the_pipe[0]);
    close(the_pipe[1]);
    return -1;
  }

  if (cpid == 0) {
    close(the_pipe[0]);
    // replace stdout with the write end of the pipe, execute the command
    int backupfd = dup(STDOUT_FILENO);
    dup2(the_pipe[1], STDOUT_FILENO);
    //execute
    map<string, command>::iterator cmd = builtins.find(tokens[0]);
    int return_value;
    if (cmd == builtins.end()) {
      return_value = execute_external_command(tokens);
    } else {
      return_value = ((*cmd->second)(tokens));
    }

    // replace stdout
    dup2(backupfd, STDOUT_FILENO);
    close(backupfd);
    close(the_pipe[1]);
    exit(return_value);
    cout << "NEVER HERE\n";
  } else {
    // parent, wait for child and then return descriptor to the read end so
    // it can be passed along
    close(the_pipe[1]);
    int status;
    if(wait(&status) == -1)
      perror("wait");

    return the_pipe[0];
  }
}


// Executes a line of input by either calling execute_external_command or
// directly invoking the built-in command.
int execute_line(vector<string>& tokens, map<string, command>& builtins) {
  // Copy incase file redirection occurs
  int stdoutcopy = dup(STDOUT_FILENO);
  int stdincopy = dup(STDIN_FILENO);

  int return_value = 0; 

  if (tokens.size() != 0) {
    // Setup file redirection on the last command
    int descriptor = redirectionScan(tokens);
    if (descriptor == -1) return -1; 

    // Split the line by pipes
    vector< vector<string> > splitline = splitByPipes(tokens);
    // Check for any invalid pipes
    if (splitline.size() == 0) {
      cerr << "Invalid pipes\n";
      // TODO: find a better way to do this since it is copied later
      dup2(stdoutcopy, STDOUT_FILENO);
      dup2(stdincopy, STDIN_FILENO);
      close(stdoutcopy);
      close(stdincopy);
      return -1;
    }

    // Execute each piece of the split with appropriate file descriptors
    for (int i = 0; i < splitline.size(); i++) {
      map<string, command>::iterator cmd = builtins.find(splitline[i][0]);

      // IMPLEMENTATION OF ONE PIPE
      if ( splitline.size() == 2 ) {
        // execute first command in a child process, get the descriptor for
        // the pipes read end back
        int readfd = piped_execution(splitline[0]);
        // check for error
        if (readfd == -1) return -1;
        // set stdin to this fd
        int backupfd = dup(STDIN_FILENO);
        dup2(readfd, STDIN_FILENO);
        // now execute the next command
        if (cmd == builtins.end()) {
          return_value = execute_external_command(splitline[1]);
        } else {
          return_value = ((*cmd->second)(splitline[1]));
        }
        // restore std in and close readfd
        close(readfd);
        dup2(backupfd, STDIN_FILENO);
        close(backupfd);
        return return_value;
      }
      // END OF ONE PIPE IMPLEMENTATION
      
      else if (cmd == builtins.end()) {
        return_value = execute_external_command(splitline[i]);
      } else {
        return_value = ((*cmd->second)(splitline[i]));
      }
    }
  }

  // Revert the std in and out
  dup2(stdoutcopy, STDOUT_FILENO);
  dup2(stdincopy, STDIN_FILENO);
  close(stdoutcopy);
  close(stdincopy);

  return return_value;
}


// Substitutes any tokens that start with a $ with their appropriate value, or
// with an empty string if no match is found.
void variable_substitution(vector<string>& tokens) {
  vector<string>::iterator token;

  for (token = tokens.begin(); token != tokens.end(); ++token) {

    if (token->at(0) == '$') {
      string var_name = token->substr(1);

      if (getenv(var_name.c_str()) != NULL) {
        *token = getenv(var_name.c_str());
      } else if (localvars.find(var_name) != localvars.end()) {
        *token = localvars.find(var_name)->second;
      } else {
        *token = "";
      }
    }
  }
}

// Substitutes !! or !N with the command in history using the readline/history library
void history_substitution(char* &line) {
  // check for a bang
  if (line[0] && line[1] && line[0] == '!') {
    // Check for another bang to replace line with most recent command
    if (line[1] == '!') {
      if (history_length > 0) {
        // Get the most recent command
        const char* newline = history_get(history_length)->line;
        // replace line with historical command
        free(line);
        line = (char*) malloc(strnlen(newline, 1024) + 1);
        strcpy(line, newline);
        line[strnlen(newline, 1024) + 1] = '\0';
      }
    }
    // Check for a number to replace with the desired command
    else if (isdigit(line[1])) {
      // Convert the rest of the first token into a string
      string numberStr;
      int i = 1;
      while (line[i] && line[i] != ' ') {
        numberStr.push_back(line[i]);
        i++;
      }
      // Get the digit from the string
      int pos = atoi(numberStr.c_str());
      // Replace the string with the command if it exists
      if (pos != 0 && history_length >= pos) {
        const char* newline = history_get(pos)->line;
        //replace
        free(line);
        line = (char*) malloc(strnlen(newline, 1024) + 1);
        strcpy(line, newline);
        line[strnlen(newline, 1024) + 1] = '\0';
      }
    }
  }
}

// Substitutes the first token for its alias if there is one
void alias_substitution(vector<string>& tokens) {
  // See if token exists
  typedef map<string, string>::iterator it;
  it posptr = aliases.find(tokens[0]);
  if (posptr != aliases.end()) {
    // Found an alias, replace the token
    tokens[0] = posptr->second;
  }
}


// Examines each token and sets an env variable for any that are in the form
// of key=value.
void local_variable_assignment(vector<string> tokens) {
  vector<string>::iterator token = tokens.begin();

  while (token != tokens.end()) {
    string::size_type eq_pos = token->find("=");

    // If there is an equal sign in the token, assume the token is var=value
    if (eq_pos != string::npos) {
      string name = token->substr(0, eq_pos);
      string value = token->substr(eq_pos + 1);

      localvars[name] = value;

      token = tokens.erase(token);
    } else {
      ++token;
    }
  }
}


// The main program
int main() {
  // Populate the map of available built-in functions
  builtins["ls"] = &com_ls;
  builtins["cd"] = &com_cd;
  builtins["pwd"] = &com_pwd;
  builtins["alias"] = &com_alias;
  builtins["unalias"] = &com_unalias;
  builtins["echo"] = &com_echo;
  builtins["exit"] = &com_exit;
  builtins["history"] = &com_history;

  // Specify the characters that readline uses to delimit words
  rl_basic_word_break_characters = (char *) WORD_DELIMITERS;

  // Tell the completer that we want to try completion first
  rl_attempted_completion_function = word_completion;

  // The return value of the last command executed
  int return_value = 0;

  // Loop for multiple successive commands 
  while (true) {

    // Get the prompt to show, based on the return value of the last command
    string prompt = get_prompt(return_value);

    // Read a line of input from the user
    char* line = readline(prompt.c_str());

    // If the pointer is null, then an EOF has been received (ctrl-d)
    if (!line) {
      break;
    }

    // If the command is non-empty, attempt to execute it
    if (line[0]) {

      // Check for !! or !N to replace the line with the history
      history_substitution(line);

      // Add this command to readline's history
      add_history(line);

      // Break the raw input line into tokens
      vector<string> tokens = tokenize(line);

      // Handle local variable declarations
      local_variable_assignment(tokens);

      // Substitute variable references
      variable_substitution(tokens);

      // Substitue command for alias if it exists, also handles !! and !N
      alias_substitution(tokens);

      // Execute the line
      return_value = execute_line(tokens, builtins);
    }

    // Free the memory for the input string
    free(line);
  }

  return 0;
}
