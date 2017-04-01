#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <locale.h>
#include <wchar.h>
#include <wctype.h>

#include <x86_64-linux-gnu/sys/types.h>
#include <x86_64-linux-gnu/sys/stat.h>
#include <x86_64-linux-gnu/sys/wait.h>

#define ARGS_COUNT 3
#define BUFFER_SIZE (16 * 1024)

typedef struct
{
	int bytes_amount;
	int words_amount;
} file_content;

char *prog_name;
int max_working_processes_amount;
int working_processes_amount;

void print_error(char *prog_name, char *error_message, char *error_file)
{
	fprintf(stderr, "%s: %s %s\n", prog_name, error_message, error_file ? error_file : "");
}

void print_results(int pid, char *full_path, int bytes_amount, int words_amount)
{
	printf("%d %s %d %d\n", pid, full_path, bytes_amount, words_amount);
}

void process_data_in_buffer(int *is_word, const char *current_char, ssize_t bytes_read, int *words_amount)
{
 	ssize_t byte_size;
  	int is_error = 0;
  	wchar_t wide_char = 0;
	mbstate_t state = {};
	
  	do
  	{
    	byte_size = (ssize_t)mbrtowc(&wide_char, current_char, (size_t)bytes_read, &state);

    	if (byte_size == (size_t) - 2)
      		is_error = 1;
    	else if (byte_size == (size_t) - 1)
    	{
      		current_char++;
      		bytes_read--;
    	}
    	else if (bytes_read > 0)
    	{
      		if (byte_size == 0)
      		{
        		wide_char = 0;
        		byte_size = 1;
      		}

      		current_char += byte_size;
      		bytes_read -= byte_size;

  			if (*is_word)
  			{
    			if (iswspace((wint_t)wide_char) || (wide_char == L' '))
      				*is_word = 0;
  			}
  			else if (iswprint((wint_t)wide_char) && (wide_char != L' '))
  			{
    			*words_amount += 1;
    			*is_word = 1;
  			}
    	}
  	} while (!is_error && (bytes_read > 0));
}

file_content *get_words_amount(char *file_name, int size)
{
	ssize_t bytes_read;
	char buf[BUFFER_SIZE];
	int words_amount = 0;

	file_content *content = malloc(sizeof(file_content));
	content->words_amount = 0;
	content->bytes_amount = 0;

	int f = open(file_name, O_RDONLY);

	if (f == -1) 
	{
		print_error(prog_name, strerror(errno), file_name);
        return NULL; 
    }

	int is_data_readed;
	int is_word = 0;

    do
    {
    	bytes_read = read(f, buf, BUFFER_SIZE);
    	is_data_readed = bytes_read > 0;
    	if (is_data_readed)
    	{
      		content->bytes_amount += (int)bytes_read;
      		process_data_in_buffer(&is_word, buf, bytes_read, &words_amount);
    	}
    } while (is_data_readed);

    content->words_amount = words_amount;

	if (close(f) == -1) {
        print_error(prog_name, "Error closing file ", file_name);
        return NULL;
    }

	return content;
}

void count_files_words(char *dir_name)
{
	DIR *directory = opendir(dir_name);
	if (!directory)
	{
		print_error(prog_name, strerror(errno), dir_name);
		return;
	}

	struct dirent *dir_entry;

	errno = 0;

	while ((dir_entry = readdir(directory)) != NULL)
	{
		struct stat file_info;

		char *file_name = (char *)malloc((strlen(dir_entry->d_name) + 1) * sizeof(char));
        strcpy(file_name, dir_entry->d_name);
        char *full_path = malloc((strlen(dir_name) + strlen(file_name) + 2) * sizeof(char));	

        if (strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0)
            continue;

	strcpy(full_path, dir_name);		
	strcat(full_path, "/");
        strcat(full_path, file_name);

        if (lstat(full_path, &file_info) == -1)
        {
            print_error(prog_name, strerror(errno), file_name);
            continue;
        }

        if (S_ISDIR(file_info.st_mode))
        {
            count_files_words(full_path);
        }
        else if (S_ISREG(file_info.st_mode))
        {
       		if (working_processes_amount >= max_working_processes_amount)
       		{
       			wait(NULL);
       			working_processes_amount -= 1;
       		}

            pid_t pid = fork();
            
            if (pid == 0)
            {
            	if (get_words_amount(full_path, (int)file_info.st_size)!= NULL)
            		print_results(getpid(), full_path, get_words_amount(full_path, (int)file_info.st_size)->bytes_amount, get_words_amount(full_path, (int)file_info.st_size)->words_amount);
            	exit(0);
            }
            else if (pid < 0)
            {
            	print_error(prog_name, "Error creating process", NULL);
            	exit(1);
            }

            working_processes_amount += 1;
        }
	}

	if (errno)
	{
		print_error(prog_name, strerror(errno), dir_name);
	}

	if (closedir(directory) == -1)
    {
        print_error(prog_name, strerror(errno), dir_name);
    }
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");

	prog_name = basename(argv[0]);
	max_working_processes_amount = atoi(argv[2]);

	if (argc != ARGS_COUNT)
	{
		print_error(prog_name, "Wrong args amount", NULL);
		return 1;
	}

	if (max_working_processes_amount == 0 || max_working_processes_amount == 1)
	{
		print_error(prog_name, "Incorrect second arg value", NULL);
		return 1;
	}

	char *dir_name = realpath(argv[1], NULL);
	if (dir_name == NULL)
	{
		print_error(prog_name, strerror(errno), argv[1]);
		return 1;
	}

	working_processes_amount = 1;
	count_files_words(dir_name);

	while (wait(NULL) > 0) {}

	return 0;
}
