/**************************************************************************************
 * Advanced OS, Assignment #1 -  File Explorer
 * Roll No: 2021201068
 * Name: Shaon Dasgupta
***************************************************************************************/

#include <sstream>
#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <stdint.h>
#include <cstring>
#include <termios.h>
#include <ctype.h>
#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fstream>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#define GetCurrentDirectory getcwd
#define clear_terminal() printf("\033c")
#define gotoxy(x,y) printf("\033[%d;%dH", (y), (x))

using namespace std;

struct termios orig_termios;
vector<char*> back_tracking_stack;
vector<char*> forward_tracking_stack;
vector<pair<char*,char>> current_items; // path, [d|f] -> representing directory or file.
int cursor_X;
int cursor_Y;

/**
 * Command Types related to the command mode.
*/
enum command_type {
    COPY,
    MOVE,
    RENAME,
    CREATE_FILE,
    CREATE_DIR,
    DELETE_FILE,
    DELETE_DIR,
    GOTO,
    SEARCH,
    UNKNOWN
};


/**************************************************************************************
 * Command Utility Function Declarations *
***************************************************************************************/
void copy_string(char* &strA, const char* strB);
void split_string(string str, vector<string> &out_tokens);
void join_paths(const char* pathA, const char* pathB, char* &path_out);
int path_exists(const char *path);
int file_exists(const char *path);
int is_directory(const char *path);
void get_name_from_path(const char* path, char* &out_name);
void get_parent_dir(char* current_dir, char* &out_parent_dir);
void get_absolute_path(char* curr_dir, const char* path, char* &abs_path);
void get_home_dir(char* &home_dir);
bool search_recursively(char* curr_dir_path, const char* name);
int remove_dir(char* abs_dir_path);
void copy_file(const char* src_path, const char* file_name, const char* dest_dir_path);
void copy_dirs(const char* src_path, const char* dir_name, const char* dest_path);


/**************************************************************************************
 * Print Utility Function Declarations *
***************************************************************************************/
void print_perms(mode_t perms);
void print_spaces(int n);
void print_name_in_twenty_three_chars(char* name);
void print_human_readable_size(long size);
void print_all_containing_items_with_details(char* folder_path, int offset, int no_of_rows, int& tot_no_of_row);
void print_in_status_bar(const char* message);
void update_view();

/**************************************************************************************
 * Terminal Utility Function Declarations *
***************************************************************************************/
void disable_raw_mode();
void enable_raw_mode();


/**************************************************************************************
 * Command Implementations *
***************************************************************************************/

/** COPY File/Folder from one location to another.
 * 
 *  Command format: copy <source_files(s)> <destination_directory>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The space separated arguments in the original command are passed into
 *  this function as a vector. The last argument is the destination path.
*/
void copy_command(char* curr_dir, vector<string> arguments){
        if(arguments.size() < 2){
            // display error
            cout << "\nThere should be atleast 2 arguments.\n";
            cursor_Y+=2;
            return;
        }

        int last_idx = arguments.size() - 1;
        const char* destination_path = arguments[last_idx].c_str();
        // Check if destination folder is present.
        errno = 0;      
        int result = mkdir(destination_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if(result == -1){
            switch(errno){
            case EACCES :
                printf("the parent directory does not allow write");
                break;
            case EEXIST:
                // This is not an error in our case.
                break;
            case ENAMETOOLONG:
                printf("pathname is too long");
                break;
            default:
                perror("mkdir");
                break;
            }
        }
        char* abs_dest_path;
        get_absolute_path(curr_dir, destination_path, abs_dest_path);

        if(!is_directory(abs_dest_path)){
           cout << "\nDestination should be a directory.\n";
           cursor_Y+=2;
           return;
        }

        for(int i=0; i<last_idx; i++) {
            const char* temp = arguments[i].c_str();
            char* full_src_path;
            get_absolute_path(curr_dir, temp, full_src_path);
            // join_paths(curr_dir, temp, full_src_path);
            if (!path_exists(full_src_path)) {
                cout << "\n" << full_src_path << " does not exist.\n";
                cursor_Y+=2;
                return;
            }

            // copy directory
            if(is_directory(full_src_path)){
                char* dir_name;
                get_name_from_path(temp, dir_name);                
                copy_dirs(full_src_path, dir_name, abs_dest_path); 
            }
            // copy file
            else {
                char* file_name;
                get_name_from_path(temp, file_name);
                copy_file(full_src_path, file_name, abs_dest_path);
            }
        }
        update_view();
}

/** MOVE File/Folder from one location to another.
 * 
 *  Command format: move <source_files(s)> <destination_directory>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The space separated arguments in the original command are passed into
 *  this function as a vector. The last argument is the destination path.
*/
void move_command(char* curr_dir, vector<string> arguments) {
     if(arguments.size() < 2){
            // display error
            cout << "\nThere should be atleast 2 arguments.\n";
            cursor_Y+=2;
            return;
        }

        int last_idx = arguments.size() - 1;
        const char* new_location = arguments[last_idx].c_str();
        char* abs_new_location;
        get_absolute_path(curr_dir, new_location, abs_new_location);

        if(!is_directory(abs_new_location)){
                cout << "\nThe destination should be a directory.\n";
                cursor_Y+=2;
                return;
        }

        for(int i=0;i < last_idx; i++){
            char* old_abs_path;
            const char* old_path_or_name = arguments[i].c_str();
            get_absolute_path(curr_dir, old_path_or_name, old_abs_path);
            if(!path_exists(old_abs_path)){
               
                    cout << "\nSource path does not exist.\n";
                    cursor_Y+=2;
                    return;
            }

            char* file_or_folder_name;
            get_name_from_path(old_abs_path ,file_or_folder_name);

            char* new_exact_full_path;
            join_paths(abs_new_location, file_or_folder_name, new_exact_full_path);

            rename((const char*)old_abs_path, (const char*)new_exact_full_path);
        }
        update_view();
}

/** RENAME File/Folder.
 * 
 *  Command format: rename <old_fileName> <new_fileName>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The space separated arguments in the original command are passed into
 *  this function as a vector. The no. of arguments in this case is restricted to 2.
*/
void rename_command(char* curr_dir, vector<string> arguments){
        if(arguments.size() != 2){
            // display error
            cout << "\nThere should be exactly 2 arguments.\n";
            cursor_Y+=2;
            return;
        }

        const char* new_name = arguments[1].c_str();
        const char* old_name = arguments[0].c_str();
        char* new_abs_path;
        get_absolute_path(curr_dir, new_name, new_abs_path);
        char* old_abs_path;
        get_absolute_path(curr_dir, old_name, old_abs_path);
        char* new_location_copy;

        char* parent_dest_path;
        get_parent_dir(new_abs_path, parent_dest_path);

        if(!path_exists(old_abs_path)){
            cout << "ERROR: path - " << old_abs_path << " does not exist.";
            return;
        }
        if(parent_dest_path == NULL || !path_exists(parent_dest_path)){
            cout << "ERROR: path - " << parent_dest_path << " does not exist.";
            return;
        } 

        rename((const char*)old_abs_path, (const char*)new_abs_path);
        update_view();
}

/** CREATE-FILE.
 * 
 *  Command format: create-file <fileName>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The fileName as the first element of a vector.
*/
void create_file_command(char* curr_dir, vector<string> arguments) {
     if(arguments.size() != 1){
            // display error
            cout << "\nThere should be exactly 1 argument for this command.\n";
            cursor_Y+=2;
            return;
        }
        const char* file_name = arguments[0].c_str();
        char* file_abs_path;
        get_absolute_path(curr_dir, file_name, file_abs_path);
        char* parent_dir;
        get_parent_dir(file_abs_path, parent_dir);
        if(!path_exists(parent_dir)){
            cout << "ERROR: Location \""<< parent_dir << "\" does not exist.";
            return;
        }
    
        fstream file;
        file.open(file_abs_path,ios::out);
        update_view();
}

/** CREATE-DIR - Create Directory
 * 
 *  Command format: create-dir <dirName>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The fileName as the first element of a vector.
*/
void create_dir_command(char* curr_dir, vector<string> arguments){
    if(arguments.size() != 1){
            // display error
            cout << "\nThere should be exactly 1 argument for this command.\n";
            cursor_Y+=2;
            return;
        }
        const char* folder_name = arguments[0].c_str();
        char* folder_abs_path;
        get_absolute_path(curr_dir, folder_name, folder_abs_path);
        char* parent_dir;
        get_parent_dir(folder_abs_path, parent_dir);
        if(!path_exists(parent_dir)){
            cout << "ERROR: Location \""<< parent_dir << "\" does not exist.";
            return;
        }
    
        mkdir(folder_abs_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        update_view();
} 

/** DELETE-FILE.
 * 
 *  Command format: delete-file <dirName>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The fileName as the first element of a vector.
*/
void delete_file_command(char* curr_dir, vector<string> arguments){
        if(arguments.size() != 1){
            // display error
            cout << "\nThere should be exactly 1 argument for this command.\n";
            cursor_Y+=2;
            return;
        }
        const char* file_name = arguments[0].c_str();
        char* file_abs_path;
        get_absolute_path(curr_dir, file_name, file_abs_path);

        if(!path_exists(file_abs_path)){
            cout << "ERROR: File \""<< file_abs_path << "\" does not exist.";
            return;
        }
    
        remove(file_abs_path);
        update_view();
}

/** DELETE-DIR - Delete Directory.
 * 
 *  Command format: delete-file <dirName>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The fileName as the first element of a vector.
*/
void delete_dir_command(char* curr_dir, vector<string> arguments){
    if(arguments.size() != 1){
            // display error
            cout << "\nThere should be exactly 1 argument for this command.\n";
            cursor_Y+=2;
            return;
        }
        const char* dir_name = arguments[0].c_str();
        char* dir_abs_path;
        get_absolute_path(curr_dir, dir_name, dir_abs_path);

    if(!path_exists(dir_abs_path)){
            cout << "ERROR: Directory \""<< dir_abs_path << "\" does not exist.";
            return;
        }
    
    remove_dir(dir_abs_path);
    update_view();

}

/** GOTO - Go to specific Location.
 * 
 *  Command format: goto <path>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The path as the first element of a vector.
*/
void goto_command(char* curr_dir, vector<string> arguments){
    if(arguments.size() != 1){
        // display error
        cout << "\nThere should be exactly 1 argument for this command.\n";
        cursor_Y+=2;
        return;
    }
    const char* path = arguments[0].c_str();
    char* abs_path;

    get_absolute_path(curr_dir, path, abs_path);
    if(!is_directory(abs_path)){
            cout << "ERROR: The destination should be a directory path.";
            return;
    }
    if(!path_exists(abs_path)){
            cout << "ERROR: File \""<< abs_path << "\" does not exist.";
            return;
        }
    back_tracking_stack.push_back(abs_path);
    update_view();
}

/** SEARCH - Search for file or folder under current directory..
 * 
 *  Command format: search <file/folder name>
 *  @param curr_dir - Current Directory Path
 *  @param arguments - The name as the first element of a vector.
*/
void search_command(char* curr_dir, vector<string> arguments){
     if(arguments.size() != 1){
        // display error
        cout << "\nThere should be exactly 1 argument for this command.\n";
        cursor_Y+=2;
        return;
    }
    const char* name = arguments[0].c_str();
    bool found = search_recursively(curr_dir, name);
    if(found){
        cout << "True";
    } else {
        cout << "False";
    }
}



/**************************************************************************************
 * Command Utility Function Implementations *
***************************************************************************************/

void copy_string(char* &strA, const char* strB){
    strA = (char*)malloc((strlen(strB)+1)*sizeof(char));
    strcpy(strA, strB);
}

void split_string(string str, vector<string> &out_tokens){
    istringstream f(str);
    string s;    
    while (getline(f, s, ' ')) {
        out_tokens.push_back(s);
    }
}

void join_paths(const char* pathA, const char* pathB, char* &path_out) {
    int lenA = strlen(pathA);
    int lenB = strlen(pathB);

    int str_out_len = lenA + lenB + 1;
    path_out = (char*)malloc((str_out_len + 1)*sizeof(char));
    memcpy(path_out, pathA, strlen(pathA));
    path_out[lenA] = '\0';
    strcat(path_out, "/");
    strcat(path_out, pathB);
}

int path_exists(const char *path){
   if(!is_directory(path)){
       if(!file_exists(path)){
           return 0;
       }
   }
   return 1;
}

int file_exists(const char *path)
{
    // Try to open file
    FILE *fptr = fopen(path, "r");

    // If file does not exists 
    if (fptr == NULL)
        return 0;

    // File exists hence close file and return true.
    fclose(fptr);

    return 1;
}

int is_directory(const char *path) {
   struct stat statbuf;
   if (stat(path, &statbuf) != 0)
       return 0;
   return S_ISDIR(statbuf.st_mode);
}

void get_name_from_path(const char* path, char* &out_name) {
    int len = strlen(path);
    int pos = len -1;
    for(int i=len - 2; i>=0; i--){
        if(path[i] == '/' || path[i] == '\\'){
            pos = i;
            break;
        }
    }
    out_name = NULL;
    if(pos < len - 1 && pos >= 0){
        int len_of_part_path = len - pos - 1;
        out_name = (char*)malloc((len_of_part_path+1)*sizeof(char)); // +1 for the ending '\0' character.
        for(int i=pos + 1; i < len; i++) {
            out_name[i-pos-1] = path[i];
        }
        out_name[len_of_part_path] = '\0';
    }else {
        out_name = (char*)malloc((strlen(path)+1)*sizeof(char)); // +1 for the ending '\0' character.
        strcpy(out_name, path);
    }
}

void get_parent_dir(char* current_dir, char* &out_parent_dir) {
    int len = strlen(current_dir);
    int pos = len -1;
    for(int i=len - 2; i>=0; i--){
        if(current_dir[i] == '/' || current_dir[i] == '\\'){
            pos = i;
            break;
        }
    }
    out_parent_dir = NULL;
    if(pos < len - 1 && pos){
        int len_of_part_path = pos;
        out_parent_dir = (char*)malloc((len_of_part_path+1)*sizeof(char)); // +1 for the ending '\0' character.
        for(int i=0; i < len_of_part_path; i++) {
            out_parent_dir[i] = current_dir[i];
        }
        out_parent_dir[len_of_part_path] = '\0';
    }
}

void get_absolute_path(char* curr_dir, const char* path, char* &abs_path){
    int len = strlen(path);
    if(len > 0){
        if(path[0] == '/') {
            copy_string(abs_path, path);
            return;
        } else if(path[0] == '~') {
            char* home_dir;
            get_home_dir(home_dir);
            if(home_dir == NULL) {
                cout << "ERROR: Could not find home directory.";
                exit(0);
            } else {
                if(len>=2 && path[1] == '/')
                    join_paths(home_dir, path+2, abs_path);
                else 
                    join_paths(home_dir, path+1, abs_path);
                return;
            }
        } else {
            join_paths(curr_dir, path, abs_path);
        }
    }
}

void get_home_dir(char* &home_dir){
  register struct passwd *pw;
  register uid_t uid;
  int c;
  uid = geteuid ();
  pw = getpwuid (uid);
  home_dir = NULL;
  if (pw)
    {
        char* usr_name = pw->pw_name;
        char home[] = "/home";
        join_paths(home, usr_name, home_dir);
    }
}

bool search_recursively(char* curr_dir_path, const char* name) {
    DIR *dir = opendir(curr_dir_path);
    int len = strlen(curr_dir_path);
    bool found = false;
    if(dir){
        struct dirent *de;
        while((de = readdir(dir))){
            if(!strcmp(de->d_name, "..") || !strcmp(de->d_name, ".")){
                continue;
            }
            if(strcmp(de->d_name, name) == 0){
                return true;
            }else {
                char* new_path;
                join_paths(curr_dir_path, de->d_name, new_path);
                found = search_recursively(new_path, name);
                if(found)
                    return true;
            }
        }
    }
    return found;
}

int remove_dir(char* abs_dir_path){
    DIR *dir = opendir(abs_dir_path);
    int len = strlen(abs_dir_path);
    int r = -1;
    if(dir){
        struct dirent *de;
        r=0;
        while(!r && (de = readdir(dir))){
            int r2 = -1;
            char* new_path;
            int l;

            if(!strcmp(de->d_name, "..") || !strcmp(de->d_name, ".")){
                continue;
            }
            l = len + strlen(de->d_name) + 2;
            
            join_paths(abs_dir_path, de->d_name, new_path);
            if(new_path){
                struct stat statbuf;
                if(!stat(new_path, & statbuf)) {
                    if(S_ISDIR(statbuf.st_mode)){
                        r2 = remove_dir(new_path);
                    }else {
                        r2 = unlink(new_path);
                    }
                }
                free(new_path);
            }
            r = r2;
        }
        closedir(dir);
    }
    if(!r)
        r = rmdir(abs_dir_path);
    return r;
}

void copy_file(const char* src_path, const char* file_name, const char* dest_dir_path){
    // Create new file path.
    char* out_new_dest_path;
    join_paths(dest_dir_path, file_name, out_new_dest_path);
    char* out_absolute_src_path;
    if (!path_exists(file_name)) {
        //relative
        join_paths(src_path, file_name, out_absolute_src_path);
    }else{
        //abs
        out_absolute_src_path = (char*)malloc((strlen(file_name)+1)*sizeof(char));
        strncpy(out_absolute_src_path, file_name, strlen(file_name)+1);
    }
    unsigned char buffer[4096];
    int  src_file = open(out_absolute_src_path, O_RDONLY);
    int  dest_file = open(out_new_dest_path, O_CREAT | O_WRONLY);

    while (1) {
        int res = read(src_file, buffer, 4096);
        if (res == -1) {
            printf("Error reading file.\n");
            cursor_Y+=2;
            exit(1);
        }
        int n = res;

        if (n == 0) break;

        res = write(dest_file, buffer, n);
        if (res == -1) {
            printf("Error writing to file.\n");
            cursor_Y+=2;
            exit(1);
        }
    }
    // Copying Permissions.
    struct stat st;
    stat(out_absolute_src_path, &st);
    chmod(out_new_dest_path, st.st_mode);

    close(src_file);
    close(dest_file);
}

void copy_dirs(const char* src_path, const char* dir_name, const char* dest_path){
    DIR *dr = opendir(src_path);
    if(dr == NULL) // opendir returns NULL if couldn't open directory.
    {
        cout << "\nCould not open directory.\n";
        cursor_Y+=2;
        return;
    }
    // Create new dir path.
    char* new_dest_path;
    join_paths(dest_path, dir_name, new_dest_path);
    int status = mkdir(new_dest_path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    struct dirent *de; 
    struct stat statbuf;
    while((de = readdir(dr)) != NULL) {
        errno = 0;
        char* out_inner_src_path;
        join_paths(src_path, de->d_name, out_inner_src_path);
        if(stat(out_inner_src_path, &statbuf)==-1) {
            continue;
        }
        if(strcmp(de->d_name, "..") && strcmp(de->d_name, ".")){
        // If its a directory.
            if((S_ISDIR(statbuf.st_mode))){
                copy_dirs(out_inner_src_path, de->d_name, new_dest_path);
                // Copying Permissions.
                struct stat st;
                stat(out_inner_src_path, &st);
                chmod(new_dest_path, st.st_mode);
            }else {
                // If its a file.
                char* file_name;
                get_name_from_path(de->d_name, file_name);
                copy_file(src_path, de->d_name, new_dest_path);
            }
        }     
    }
}


/**************************************************************************************
 * Print Utility Function Implementations *
***************************************************************************************/

void print_perms(mode_t perms) {
    printf((S_ISDIR(perms)) ? "d" : "-");
	printf((perms & S_IRUSR) ? "r" : "-");
	printf((perms & S_IWUSR) ? "w" : "-");
	printf((perms & S_IXUSR) ? "x" : "-");
	printf((perms & S_IRGRP) ? "r" : "-");
	printf((perms & S_IWGRP) ? "w" : "-");
	printf((perms & S_IXGRP) ? "x" : "-");
	printf((perms & S_IROTH) ? "r" : "-");
	printf((perms & S_IWOTH) ? "w" : "-");
	printf((perms & S_IXOTH) ? "x" : "-");
}
/**Returns string with n spaces*/
void print_spaces(int n) {
    char* s = (char *)malloc((n+1)*(sizeof(char)));
    for(int i=0; i<n; i++){
        s[i] = ' ';
    }
    s[n] = '\0';
    cout << s;
}

void print_name_in_twenty_three_chars(char* name) {
    long len;
    len = strlen(name);
    if(len > 23){
        char sub_name[21];
        memcpy(sub_name, name, 20);
        sub_name[20] = '\0';
        char elipses[] = "...";
        strcat(sub_name, elipses);
        cout << sub_name;
    }
    else if(len < 23){
        cout << name;
        print_spaces(23 - len);
    }else {
        cout << name;
    }
}

void print_human_readable_size(long size) {
    char units[] = { '\0', 'K', 'M', 'G', 'T', 'P', 'E', 'Z'};
    // change to double
    double d_size = size;
    for(int i=0; i<8; i++){
        if(d_size < 1024){
            printf("%lf %cB", d_size, units[i]);
            return;
        }
        d_size = d_size/1024;
    }
}

void print_all_containing_items_with_details(char* folder_path, int offset, int no_of_rows, int& tot_no_of_row){
    DIR *dr = opendir(folder_path);
    if(dr == NULL) // opendir returns NULL if couldn.t open directory.
    {
        cout << "Could not open directory.";
        return;
    }
    struct dirent *de; 
    struct stat statbuf;
    current_items.clear();
    int count = 0;
    while((de = readdir(dr)) != NULL) {
        count++;
        if(count >= offset && (count - offset) < no_of_rows) {
            // int newPathLen = strlen(folder_path) + strlen(de->d_name) + 1;
            char* newPath;
            if(!strcmp(de->d_name, "..")){
                get_parent_dir(folder_path, newPath);
            }else {
                join_paths(folder_path, de->d_name, newPath);
            }
            if(stat(newPath, &statbuf)==-1){
                continue;
            }
            print_name_in_twenty_three_chars(de->d_name);
            char d_or_f = (S_ISDIR(statbuf.st_mode)) ? 'd' : 'f';           
            current_items.push_back({newPath, d_or_f});
            cout << "\t"; //print name of every dir or file in the current dir.
            print_human_readable_size(statbuf.st_size);
            cout << "\t\t";
            struct passwd *pw = getpwuid(statbuf.st_uid);
	        struct group *gr = getgrgid(statbuf.st_gid);
	        if (pw != 0)
		        printf("%-8s", pw->pw_name);
	        if (gr != 0)
		        printf(" %-8s", gr->gr_name);
            cout << "\t\t";
            print_perms(statbuf.st_mode);
            cout << "\t\t";
            char timeStr[ 100 ] = "";
            strftime(timeStr, 100, "%d-%m-%Y %H:%M:%S", localtime( &statbuf.st_mtime));
            cout << timeStr;
            cout << "\n";
        }
    }
    closedir(dr);
    tot_no_of_row = count;
}

void print_in_status_bar(const char* message) {
    gotoxy(1, 22);
    cout << "||---" << message << "---||";
    gotoxy(1,1);
}

void update_view(){
    gotoxy(1,1);
    int tot_no_of_rows;
    print_all_containing_items_with_details(back_tracking_stack.back(),
    1,
    20,
    tot_no_of_rows);
    for(int i=1; i<=(20-tot_no_of_rows); i++) {
        printf("%c[2K\n", 27);
    }
    gotoxy(cursor_X,cursor_Y);
}

/**************************************************************************************
 * Terminal Utility Function Declarations *
***************************************************************************************/

void disable_raw_mode() {
    tcsetattr(fileno(stdin), TCSAFLUSH, &orig_termios);
}

void enable_raw_mode() {
    tcgetattr(fileno(stdin), &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    // Turn off ECHO and Canonical mode
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;  
    if (tcsetattr(fileno(stdin), TCSAFLUSH, &raw) != 0) {
	    fprintf(stderr, "\nCould not set attributes\n");
    }
}

void run_command_mode();

void run_normal_mode() {
    clear_terminal();   
    enable_raw_mode();
    if(back_tracking_stack.size()==0){
        char curr_dir[256];
        GetCurrentDirectory(curr_dir, 256); // Get the current working directory
        back_tracking_stack.push_back(curr_dir);
    }
    bool render = true;
    char c1, c2, c3;
    int cursor_positionX = 1;
    int cursor_positionY = 1;
    int start = 1;
    int no_of_rows = 20;
    int tot_no_of_rows=0;
    bool go_to_command_mode = false;
    while (1) {
        if(render == true) {
            clear_terminal();
            print_all_containing_items_with_details(back_tracking_stack.back(), start, no_of_rows, tot_no_of_rows);
            const char* status_message = "NORMAL MODE";
            print_in_status_bar(status_message);
            gotoxy(cursor_positionX, cursor_positionY);
            render = false;
        }
        if(c1 = cin.get()){
            if (iscntrl(c1)) {
                if (c1 == 27) {
                    c2 = cin.get();
                    if (c2 == '[') {
                        c3 = cin.get();
                        if (c3 == 'A') {
                            // Up arrow.
                            if(cursor_positionY > 1){
                                cursor_positionY--;
                                gotoxy(cursor_positionX, cursor_positionY);
                            }
                        } else if (c3 == 'B'){
                            // Down arrow.
                            int min = no_of_rows < tot_no_of_rows ? no_of_rows :tot_no_of_rows;
                            if(cursor_positionY < min){
                                cursor_positionY++;
                                gotoxy(cursor_positionX, cursor_positionY);
                            }
                        } else if (c3 == 'C'){
                            // Right Arrow.
                            if(forward_tracking_stack.size() > 0) {
                                back_tracking_stack.push_back(forward_tracking_stack.back());
                                forward_tracking_stack.pop_back();
                                cursor_positionX = 1;
                                cursor_positionY = 1;
                                render = true;
                            }

                        } else if (c3 == 'D'){
                            // Left Arrow.
                            if(back_tracking_stack.size() > 1) {
                                forward_tracking_stack.push_back(back_tracking_stack.back());
                                back_tracking_stack.pop_back();
                                cursor_positionX = 1;
                                cursor_positionY = 1;
                                render = true;
                            }
                        }
                    }else{
                        c1 = c2;
                    }
                }else {
                    if(c1 == 10){
                        // ENTER
                        if(current_items[cursor_positionY - 1].second == 'd'){
                            // Handle opening Directory
                            back_tracking_stack.push_back(current_items[cursor_positionY - 1].first);
                            forward_tracking_stack.clear();
                            cursor_positionX = 1;
                            cursor_positionY = 1;
                            start = 1;
                            render = true;
                        }else {
                            // Handle opening File
                            pid_t pid = fork();
                            if (pid == 0) {
                                // move the pointer to last line to display err.
                                int res = execl("/usr/bin/xdg-open", "xdg-open", current_items[cursor_positionY - 1].first, (char *)0);
                                exit(1);
                            }
                        }
                    }else if(c1 == 127){
                        // BACKSPACE
                        char* out_parent_dir;
                        char* current_dir;
                        if(back_tracking_stack.size() > 0)
                            current_dir = back_tracking_stack.back();
                        else
                            GetCurrentDirectory(current_dir, 256); // Get the current working directory
                        get_parent_dir(current_dir, out_parent_dir);
                        back_tracking_stack.push_back(out_parent_dir);
                        forward_tracking_stack.clear();
                        cursor_positionX = 1;
                        cursor_positionY = 1;
                        start = 1;
                        render = true;
                    }
                }
            } else if (c1 == 'h' || c1 == 'H'){
                  register struct passwd *pw;
                  register uid_t uid;
                  int c;
                  uid = geteuid ();
                  pw = getpwuid (uid);
                  if (pw)
                    {
                        char* usr_name = pw->pw_name;
                        char home[] = "/home";
                        char* home_full_path;
                        join_paths(home, usr_name, home_full_path);
                        back_tracking_stack.push_back(home_full_path);
                        render = true;
                    }   
            } else if (c1 == 'l' || c1 == 'L'){
                if((start + no_of_rows - 1) < tot_no_of_rows) {
                    start++;
                    render = true;
                }
            } else if (c1 == 'k' || c1 == 'K'){
                if(start > 1){
                    start--;
                    render = true;
                }
            } else if (c1 == 'q' || c1 == 'Q'){
                disable_raw_mode();
                gotoxy(1,cursor_Y+1);
                exit(0);
            } 
            else if(c1 == ':') {
                go_to_command_mode = true;
                break;
            }
        }
    }

    if(go_to_command_mode){
        run_command_mode();
    }
}

void run_command_mode() {
    const char* status_message = "COMMAND MODE";
    print_in_status_bar(status_message);
    cursor_X = 25;
    cursor_Y = 22;
    gotoxy(cursor_X, cursor_Y);
    string command;
    int rel_cursor_pos = 1;
    bool change_to_normal_mode = false;
    while(1) {
        cout << ": ";
        cursor_X +=2; 
        while(1){
        char ch = getchar();
        cout << ch;
        if(ch == 27){
            change_to_normal_mode = true;
            break;
        } else if(ch == 127 && rel_cursor_pos > 1){
            cout << "\b \b";
            rel_cursor_pos--;
            cursor_X--;
            if(command.size()>0)
               command.pop_back();
        } else if (ch == 10){
        // Run command
        // Identify command
        command_type cmd_type;
        if(command.rfind("copy ", 0) == 0) { // try trimming extra spaces later
            cmd_type = COPY;
        } else if (command.rfind("move ", 0) == 0) {
            cmd_type = MOVE;
        } else if (command.rfind("rename ", 0) == 0) {
            cmd_type = RENAME;
        } else if (command.rfind("create_file ", 0) == 0) {
            cmd_type = CREATE_FILE;
        } else if (command.rfind("create_dir ", 0) == 0) {
            cmd_type = CREATE_DIR;
        } else if (command.rfind("delete_file ", 0) == 0) {
            cmd_type = DELETE_FILE;
        } else if (command.rfind("delete_dir ", 0) == 0) {
            cmd_type = DELETE_DIR;
        } else if (command.rfind("goto ", 0) == 0) {
            cmd_type = GOTO;
        } else if (command.rfind("search ", 0) == 0) {
            cmd_type = SEARCH;
        } else {
            cmd_type = UNKNOWN;
        }

        switch(cmd_type) {
            case COPY:{
                vector<string> arguments_cpy;
                split_string(command.substr(5), arguments_cpy); // will not work with files containing spaces
                copy_command(back_tracking_stack.back(), arguments_cpy);
                break;
            }
            case MOVE: {
                vector<string> arguments_mv;
                split_string(command.substr(5), arguments_mv); // will not work with files containing spaces
                move_command(back_tracking_stack.back(), arguments_mv);
                break;
            }
            case RENAME: {
                vector<string> arguments_rename;
                split_string(command.substr(7), arguments_rename); // will not work with files containing spaces
                rename_command(back_tracking_stack.back(), arguments_rename);
                break;
            }
            case CREATE_FILE: {
                vector<string> arguments_create_file;
                split_string(command.substr(12), arguments_create_file); // will not work with files containing spaces
                create_file_command(back_tracking_stack.back(), arguments_create_file);
                break;
            }
            case CREATE_DIR: {
                vector<string> arguments_create_dir;
                split_string(command.substr(11), arguments_create_dir); // will not work with files containing spaces
                create_dir_command(back_tracking_stack.back(), arguments_create_dir);
                break;
            }
            case DELETE_FILE: {
                vector<string> arguments_delete_file;
                split_string(command.substr(12), arguments_delete_file); // will not work with files containing spaces
                delete_file_command(back_tracking_stack.back(), arguments_delete_file);
                break;
            }
            case DELETE_DIR: {
                vector<string> arguments_delete_dir;
                split_string(command.substr(11), arguments_delete_dir); // will not work with files containing spaces
                delete_dir_command(back_tracking_stack.back(), arguments_delete_dir);
                break;
            }
            case GOTO: {
                vector<string> arguments_go_to;
                split_string(command.substr(5), arguments_go_to); // will not work with files containing spaces
                goto_command(back_tracking_stack.back(), arguments_go_to);
                break;
            }
            case SEARCH: {
                vector<string> arguments_search;
                split_string(command.substr(7), arguments_search); // will not work with files containing spaces
                search_command(back_tracking_stack.back(), arguments_search);
                break;
            }
            case UNKNOWN: {
                cout << "Unknown Command.";
                break;
            }
        }
        command.clear();
        cout << "\n";
        cursor_Y+=1;
        rel_cursor_pos = 1;
        cursor_X = 1;
        break;
        }else {
            command.push_back(ch);
            cursor_X++;
            rel_cursor_pos++;
        }
        }
        if(change_to_normal_mode) {
            break;
        }
    }
    if(change_to_normal_mode) {
        run_normal_mode();
    }  
}

int main() {
    run_normal_mode();
    return 0;
}