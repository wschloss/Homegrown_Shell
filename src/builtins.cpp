#include "builtins.h"

using namespace std;

// Allow reference to the alias map for the alias command
extern map<string, string> aliases;

int com_ls(vector<string>& tokens) {
  // if no directory is given, use the local directory
  if (tokens.size() < 2) {
    tokens.push_back(".");
  }

  // open the directory
  DIR* dir = opendir(tokens[1].c_str());

  // catch an errors opening the directory
  if (!dir) {
    // print the error from the last system call with the given prefix
    perror("ls error");

    // return error
    return 1;
  }

  // output each entry in the directory
  for (dirent* current = readdir(dir); current; current = readdir(dir)) {
    cout << current->d_name << endl;
  }
  // close the dir
  closedir(dir);

  // return success
  return 0;
}


int com_cd(vector<string>& tokens) {
  // Ensure a directory was passed
  if (tokens.size() < 2) {
    tokens.push_back(".");
  }
  // Use the chdir syscall
  if (chdir(tokens[1].c_str()) != 0) {
    perror("cd error");
    return 1;
  };
  return 0;
}


int com_pwd(vector<string>& tokens) {
  // Get dir
  string curDir = pwd();
  cout << curDir << endl;
  return 0;
}


int com_alias(vector<string>& tokens) {
  // if no alias passed, list all of the current aliases
  if (tokens.size() < 2) {
    // iterator
    typedef map<string, string>::iterator it;
    for (it i = aliases.begin(); i != aliases.end(); i++) {
      // Print out the value
      cout << "alias " << i->first << "=\'" << i->second << "\'" << endl;
    }
  }
  else {
    // Define the new alias in the map
    // split the arg by =
    string arg = tokens[1];
    size_t splitIndex = arg.find("=");
    // check we found the =
    if (splitIndex != string::npos) {
      // split and asign in the map
      aliases[arg.substr(0, splitIndex)] = arg.substr(splitIndex + 1);
    } else {
      // alert of the usage
      cout << "usage: alias newname=command" << endl;
      return 1;
    }
  }

  return 0;
}


int com_unalias(vector<string>& tokens) {
  // Check that just one arg was passed (-a or a name)
  if (tokens.size() != 2) {
    cout << "usage: unalias [-a or name]" << endl;
    return 1;
  }
  // Erase all or just one alias
  if (tokens[1] == "-a")
    aliases.clear();
  else {
    // see if the alias is erased
    if (aliases.erase(tokens[1]) != 1) {
        cout << "alias '" << tokens[1] << "' was not found" << endl;
        return 1;
    }
  }
  return 0;
}


int com_echo(vector<string>& tokens) {
  // Iterate over the tokens, cout all but the first
  for (int i = 1; i < tokens.size(); i++) {
      cout << tokens[i] << " ";
  }
  cout << endl;
  return 0;
}


int com_exit(vector<string>& tokens) {
  // Print a message
  cout << "shell closed" << endl;
  // Call the exit sys call
  exit(0);
  // Shouldn't ever get here
  return 0;
} 


int com_history(vector<string>& tokens) {
  // Use the history library
  // history_get(i) returns a pointer to the line that was instruction i
  // Only print up to 100 recent commands
  int startIndex = max(1, history_length - 99);
  for (int i = startIndex; history_get(i) != NULL; i++) {
    cout << "   " << i << " " << history_get(i)->line << endl;
  }
  return 0;
}

string pwd() {
  // Define buffer
  char* curDir = (char*) malloc(sizeof(char) * 1024);
  // Syscall for current directory
  getcwd(curDir, 1024);
  // Check if got the dir correctly
  if (curDir != NULL) {
    // Cast, deallocate, and return
    string ret = (string) curDir; 
    free(curDir);
    return ret;
  }
  return NULL;
}
