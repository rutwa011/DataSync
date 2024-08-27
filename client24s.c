#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PATH_MAX 4096
#define PORT 6009
#define BUFFER_SIZE 1024

//void parse_path(const char *path, char *dir_name, char *fname);

void transmit_command(int sock_fd, const char *cmd, const char *param1, const char *param2)
{
    // Buffer to store the command string
    char cmd_buffer[BUFFER_SIZE];

    // Format the command and arguments into a single string
    snprintf(cmd_buffer, sizeof(cmd_buffer), "%s %s %s", cmd, param1, param2);

    // Send the command to the client socket
    if (send(sock_fd, cmd_buffer, strlen(cmd_buffer), 0) == -1)
    {
        // Error handling for send failure
        perror("transmit_command failed");
    }
}


void transfer_file(int sock_fd, const char *file_name)
{
    // Buffer for reading the file contents
    char file_buffer[BUFFER_SIZE];

    // Open the file in read-only mode
    int fd = open(file_name, O_RDONLY);
    if (fd < 0)
    {
        // Error handling if the file cannot be opened
        perror("Unable to open file");
        return;
    }

    // Obtain the file size using lseek
    off_t size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET); // Reset file pointer to the beginning

    // Check if the file is empty
    if (size == 0)
    {
        printf("Empty file: %s\n", file_name);
        send(sock_fd, "", 1, 0); // Send an empty string if the file is empty
    }
    else
    {
        // Read and send the file contents in chunks
        ssize_t bytes_read;
        while ((bytes_read = read(fd, file_buffer, sizeof(file_buffer))) > 0)
        {
            if (send(sock_fd, file_buffer, bytes_read, 0) == -1)
            {
                // Error handling for send failure
                perror("transfer_file error");
                continue;
            }
        }
    }

    // Close the file descriptor
    close(fd);
}


// Function to extract components of a given file path into directory, basename, and extension
void extract_path_components(const char *path, char *directory, char *basename, char *extension) {
    // Find the last occurrence of '/' in the path, which separates the directory from the filename
        const char *last_slash = strrchr(path, '/');
    const char *last_dot = strrchr(path, '.');

        // If a '/' is found, it means the path includes a directory
    if (last_slash) {
                // Calculating the length of the directory part (including the slash)
        size_t dir_length = last_slash - path + 1;
        strncpy(directory, path, dir_length);

        // Null-terminate the directory string to ensure it's a valid C string
        directory[dir_length] = '\0';  // Ensure null-termination
    } else {
        strcpy(directory, "./");  // Default to current directory if no slash found
    }

    if (last_dot && last_dot > last_slash) {
        size_t base_length = last_dot - (last_slash ? last_slash + 1 : path);
        strncpy(basename, last_slash ? last_slash + 1 : path, base_length);  // Copy the basename part into the 'basename' variable
        basename[base_length] = '\0';  // Ensure null-termination
        strcpy(extension, last_dot);  // Copy extension including the dot
    } else {
        strcpy(basename, last_slash ? last_slash + 1 : path);
        extension[0] = '\0';  // No extension found
    }
}

// Function to generate a unique filename based on an existing path
void generate_unique_filename(const char *path, char *unique_name) {
    // Buffers to hold the directory, basename, and extension components of the path
        char directory[1024], basename[1024], extension[1024];
    int counter = 0;

    // Extracting the directory, basename, and extension from the provided path
    extract_path_components(path, directory, basename, extension);

    snprintf(unique_name, 1024, "%s%s%s", directory, basename, extension);

        // Checking if a file with the generated name already exists, and if so, keep generating a new name
    while (access(unique_name, F_OK) == 0)
        {
        counter++;      // Increment the counter to generate a new filename
        snprintf(unique_name, 1024, "%s%s(%d)%s", directory, basename, counter, extension);
    }
}

int create_dir_if_new(const char *dir_path)
{
    // Temporary buffer to store the directory path
    char temp_path[256];
    char *ptr = NULL;
    size_t path_length;

    // Copy the directory path into the temporary buffer
    snprintf(temp_path, sizeof(temp_path), "%s", dir_path);
    path_length = strlen(temp_path);

    // Remove the trailing slash if it exists
    if (temp_path[path_length - 1] == '/')
    {
        temp_path[path_length - 1] = '\0';
    }

    // Iterate through the path and create directories one by one
    for (ptr = temp_path + 1; *ptr; ptr++)
    {
        if (*ptr == '/')
        {
            *ptr = '\0';  // Temporarily terminate the string

            // Attempt to create the directory with default permissions
            if (mkdir(temp_path, 0755) != 0 && errno != EEXIST)
            {
                // Print error if directory creation fails
                perror("mkdir error");
                return -1;
            }

            *ptr = '/';  // Restore the slash
        }
    }

    // All directories created successfully
    return 0;
}


void download_file(int sock_fd, const char *file_name)
{
    char recv_buffer[BUFFER_SIZE];
    FILE *output_file;
    ssize_t bytes_read;
    char final_filename[BUFFER_SIZE];
    char full_path[BUFFER_SIZE];
    char current_dir[PATH_MAX];

    // Get the current working directory
    getcwd(current_dir, sizeof(current_dir));

    // Variables to hold directory and base filename
    char directory_name[1024];
    char base_file_name[1024];
    char file_extension[1024];

    // parse_path(file_name, directory_name, base_file_name);

    // Split the given filename into directory and base filename
    extract_path_components(file_name, directory_name, base_file_name, file_extension);

    // Form the complete file path
    snprintf(full_path, sizeof(full_path), "%s/%s%s", current_dir, base_file_name, file_extension);

    // Generate a unique filename to avoid overwriting
    generate_unique_filename(full_path, final_filename);

    // Open the file for writing in binary mode
    output_file = fopen(final_filename, "wb");
    if (output_file == NULL)
    {
        // Handle error if the file cannot be opened
        perror("Error opening file for writing");
        return;
    }

    printf("Receiving file: %s\n", final_filename);

    // Receive data from the client in chunks and write to the file
    while ((bytes_read = recv(sock_fd, recv_buffer, sizeof(recv_buffer), 0)) >= 0)
    {
        fwrite(recv_buffer, 1, bytes_read, output_file);
        if (bytes_read < sizeof(recv_buffer))
        {
            break; // End of file reached
        }
    }

    // Handle error if receiving fails
    if (bytes_read < 0)
    {
        perror("Error receiving file");
    }

    // Close the file after receiving is complete
    fclose(output_file);
}


int execute_command(int sock_fd, const char *cmd, const char *arg1, const char *arg2)
{
    // Handle the "ufile" command: upload a file from the client
    if (strcmp(cmd, "ufile") == 0)
    {
        transmit_command(sock_fd, cmd, arg1, arg2);
        transfer_file(sock_fd, arg1);
    }
    // Handle the "dfile" command: download a file to the client
    else if (strcmp(cmd, "dfile") == 0)
    {
        transmit_command(sock_fd, cmd, arg1, arg2);
        download_file(sock_fd, arg1);
    }
    // Handle the "rmfile" command: remove a file
    else if (strcmp(cmd, "rmfile") == 0)
    {
        transmit_command(sock_fd, cmd, arg1, arg2);
    }
    // Handle the "dtar" and "display" commands
    else if (strcmp(cmd, "dtar") == 0 || strcmp(cmd, "display") == 0)
    {
        transmit_command(sock_fd, cmd, arg1, arg2);
    }
    // Handle unknown commands
    else
    {
        printf("Invalid command received: %s\n", cmd);
        return -1;
    }
    return 0;
}


int main()
{
    int sock_fd;                        // Socket file descriptor for communication with the server
    struct sockaddr_in server_info;     // Structure to store server's address information
    char cmd[BUFFER_SIZE], param1[BUFFER_SIZE], param2[BUFFER_SIZE];            // Buffers to store the command and its arguments
    char *cmd_token;                    // Pointer used for tokenizing user input
    char user_input[BUFFER_SIZE];       // Buffer to store the raw user input

    // Creating a TCP socket
    if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);             // Exit the program with a failure status
    }

    // Configure the server address structure
    server_info.sin_family = AF_INET;
    server_info.sin_port = htons(PORT);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.4", &server_info.sin_addr) <= 0)
    {
        perror("Invalid server IP address");
        exit(EXIT_FAILURE);
    }

    // Establish connection to the server
    if (connect(sock_fd, (struct sockaddr *)&server_info, sizeof(server_info)) < 0)
    {
        perror("Could not connect to the server");
        exit(EXIT_FAILURE);
    }

    // Display usage instructions for various commands
    printf("Usage for ufile: ufile filename_in_client filepath_in_smain \n");
    printf("Usage for dfile: dfile filepath_in_smain/filename \n");
    printf("Usage for rmfile: rmfile filepath_in_smain/filename \n");
    printf("Usage for dtar command: dtar file_extension (Eg: dtar .c/.pdf/txt) \n");
    printf("Usage for display command: display filepath/pathname (inside smain) \n");
    while (1)
    {
        // Prompt the user for input
        printf("\nEnter command: ");
        if (fgets(user_input, sizeof(user_input), stdin) == NULL)
        {
            perror("Not able to read the input");
            continue;           // Skipping the rest of the loop and prompt the user again
        }

        // Remove the newline character from the input
        user_input[strcspn(user_input, "\n")] = 0;

        // Tokenize the input string to extract the command and arguments
        cmd_token = strtok(user_input, " ");
        if (cmd_token == NULL)
        {
            printf("No command entered. Please try again.\n");
            continue;
        }

        // Copy the command and arguments into respective buffers
        strncpy(cmd, cmd_token, sizeof(cmd));
        cmd_token = strtok(NULL, " ");
        if (cmd_token != NULL)
        {
            strncpy(param1, cmd_token, sizeof(param1));
            cmd_token = strtok(NULL, " ");
            if (cmd_token != NULL)
            {
                // Copy the second argument into the 'param2' buffer
                strncpy(param2, cmd_token, sizeof(param2));
            }
            else
            {
                param2[0] = '\0';       // If no second argument, set 'param2' to an empty string
            }
        }
        else
        {
            param1[0] = '\0';           // If no first argument, set 'param1' to an empty string
            param2[0] = '\0';
        }

        // Execute the command by calling the appropriate handler

        if (execute_command(sock_fd, cmd, param1, param2) == -1)
        {
            // Handle the error in command execution
            printf("Error: Command execution failed. Please check the command and try again.\n");
            continue;
        }

        // Optional: Receive and display the server's response
        char server_reply[BUFFER_SIZE];
        int bytes_received = recv(sock_fd, server_reply, sizeof(server_reply) - 1, 0);          // Receiving data from the server
        if (bytes_received > 0)
        {
            server_reply[bytes_received] = '\0'; // Null-terminate the received string
            printf("Response from the Main server connected to Client: %s\n", server_reply);    // Displaying the server's response
        }
        else if (bytes_received == 0)
        {
            // If no data is received, it means the server has disconnected
            printf("Server disconnected.\n");
            break;
        }
        else
        {
            perror("Could not receive data from server");       // Printing error message if receiving data not successful
        }
    }

    // Close the socket and end the connection
    close(sock_fd);
    return 0;
}


/*void parse_path(const char *path, char *dir_name, char *fname)
{
    // Locate the last occurrence of the '/' character
    const char *slash_pos = strrchr(path, '/');

    // If a '/' is found, separate the directory and file names
    if (slash_pos != NULL)
    {
        // Extract the directory name (including the slash)
        size_t dir_len = slash_pos - path + 1;
        strncpy(dir_name, path, dir_len);
        dir_name[dir_len] = '\0';

        // Extract the file name
        strcpy(fname, slash_pos + 1);
    }
    else
    {
        // If no '/' is found, the entire path is treated as a file name
        strcpy(dir_name, "");
        strcpy(fname, path);
    }
}
*/