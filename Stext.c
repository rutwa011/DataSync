#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

#define TEXT_PORT 6012
#define BUFFER_SIZE 1024
#define SERVER_IP "127.0.1.6"

const char *valid_home_dir()
{
    return getenv("HOME");      //Function to define the HOME directory which other functions will use as Path variable
}

int create_dir_if_new(const char *directory_path) {
    char path_copy[256];       // Temporary buffer to hold a copy of the directory path
    char *path_position = NULL; // Pointer to traverse and modify the path string
    size_t path_length;

    // Copy the directory path into the temporary buffer
    snprintf(path_copy, sizeof(path_copy), "%s", directory_path);
    path_length = strlen(path_copy);

    // Remove trailing slash if present
    if (path_copy[path_length - 1] == '/') {
        path_copy[path_length - 1] = '\0';
    }

    // Traversing the path and create directories one by one
    for (path_position = path_copy + 1; *path_position; path_position++) {
        if (*path_position == '/') {
            *path_position = '\0'; // Temporarily terminating the string to create a directory
            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                // Handle errors during directory creation
                perror("Could not create new directory");
                return -1;
            }
            *path_position = '/'; // Restoring the slash after creating the directory
        }
    }

    // Create the final directory in the path
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        perror("Error creating final directory");
        return -1;
    }

    return 0; // Return 0 to indicate success
}

// Declaring functions beforehand and then defining them later in the program based on their usage and requirement
void process_client_request(int client_socket);
void handle_upload_file(int client_socket, char *file_name, char *destination_dir, char *recv_buffer);
void handle_download_file(int client_socket, char *file_name);
void handle_remove_file(int client_socket, char *file_name);
void handle_create_tar(int client_socket, char *file_extension);
void handle_display(int client_socket, char *pathname);

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_len = sizeof(client_address);

    // Creating a TCP socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // Initialize server address structure
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(TEXT_PORT);

    // Convert IP address from string to binary form
    if (inet_pton(AF_INET, SERVER_IP, &server_address.sin_addr) <= 0)
    {
        perror("Invalid Address\n");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Bind socket to the specified IP and port
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Socket binding failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_socket, 5) < 0)
    {
        perror("Not able to listen to client connection requests");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Stext server running and waiting for connections on port %d...\n", TEXT_PORT);

    // Main server loop to accept incoming connections
    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len)) >= 0) {
        printf("Connection established with a client.\n");

        if (fork() == 0) {
            // Inside the child process
            close(server_socket);
            process_client_request(client_socket);
            close(client_socket);
            exit(0);
        } else {
            // Inside the parent process
            close(client_socket);
            wait(NULL);
        }
    }

    if (client_socket < 0) {
        perror("Failed to accept connection");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    close(server_socket);
    return 0;
}


void process_client_request(int client_socket) {
    char recv_buffer[BUFFER_SIZE];  // Buffer to store received data
    char cmd[BUFFER_SIZE];          // Buffer to store the command
    char arg1[BUFFER_SIZE], arg2[BUFFER_SIZE];  // Buffers to store command arguments

    while (1) {
        // Clear buffers before receiving new data
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
                recv_buffer[i] = 0;
                cmd[i] = 0;
                arg1[i] = 0;
                arg2[i] = 0;
        }

        // Receive data from the client
        int received_length = recv(client_socket, recv_buffer, BUFFER_SIZE, 0);
        if (received_length <= 0) {
       //     perror("Failed to receive data");
            break;
        }

        // Ensure the received data is null-terminated
        recv_buffer[received_length] = '\0';

        // Parse the received data into command and arguments
        sscanf(recv_buffer, "%s %s %s", cmd, arg1, arg2);

        // Handle the specific command received from the client
        if (strcmp(cmd, "ufile") == 0) {
            handle_upload_file(client_socket, arg1, arg2, recv_buffer);         // to handle the ufile command
        } else if (strcmp(cmd, "dfile") == 0) {
            handle_download_file(client_socket, arg1);                          // to handle the dfile command
        } else if (strcmp(cmd, "rmfile") == 0) {
            handle_remove_file(client_socket, arg1);                            // to handle the rmfile command
        } else if (strcmp(cmd, "dtar") == 0) {
            handle_create_tar(client_socket, arg1);                             // to handle the dtar command
        } else if (strcmp(cmd, "display") == 0) {
            handle_display(client_socket, arg1);                                // to handle the display command
        } else {
            // Send an error message to the client if the command is invalid
            char *error_message = "Invalid command\n";
            send(client_socket, error_message, strlen(error_message), 0);
        }
    }
}


void handle_upload_file(int client_socket, char *file_name, char *destination_dir, char *recv_buffer) {
    char full_file_path[BUFFER_SIZE];        // Full path where the file will be stored
    FILE *file_pointer;
    char server_response[BUFFER_SIZE];       // Response to be sent back to the client
    char full_destination_path[BUFFER_SIZE]; // Destination directory path
    char file_content_buffer[BUFFER_SIZE];   // Buffer to store chunks of file content
    int received_bytes;

    // Construct the full destination directory path
    snprintf(full_destination_path, sizeof(full_destination_path), "%s/stext/%s", valid_home_dir(), destination_dir);
    if (create_dir_if_new(full_destination_path) != 0) {
        return; // If directory creation fails, exit the function
    }

    // Construct the full file path with filename
    snprintf(full_file_path, sizeof(full_file_path), "%s/stext/%s/%s", valid_home_dir(), destination_dir, file_name);

    // Open the file for writing in binary mode
    file_pointer = fopen(full_file_path, "wb");
    if (file_pointer == NULL) {
        snprintf(server_response, sizeof(server_response), "Unable to open file %s for writing\n", full_file_path);
        send(client_socket, server_response, strlen(server_response), 0);
        return;
    }

    printf("Receiving file: %s\n", full_file_path);

    // Receive the file content from the client
    while ((received_bytes = recv(client_socket, file_content_buffer, sizeof(file_content_buffer), 0)) > 0) {
        printf("File has %d bytes\n", received_bytes);
        fwrite(file_content_buffer, 1, received_bytes, file_pointer); // Write the received bytes to the file
        if (received_bytes < sizeof(file_content_buffer)) {
    //      printf("End of file detected\n");
            break; // End of file reached
        }
    }

    fclose(file_pointer);

    snprintf(server_response, sizeof(server_response), "File %s uploaded to Client Directory\n", file_name);
    send(client_socket, server_response, strlen(server_response), 0); // Notify client of successful upload
}

void handle_download_file(int client_socket, char *file_name) {
    char file_read_buffer[BUFFER_SIZE];    // Buffer to hold file content being read
    char full_file_path[BUFFER_SIZE];      // Full path to the file being downloaded
    int file_descriptor;
    int read_bytes;
    char download_response[BUFFER_SIZE];   // Response to be sent back to the client

    // Construct the full path to the file
    snprintf(full_file_path, sizeof(full_file_path), "%s/stext/%s", valid_home_dir(), file_name);
    printf("Downloading file from: %s\n", full_file_path);

    // Open the file for reading
    file_descriptor = open(full_file_path, O_RDONLY);
    if (file_descriptor < 0) {
    //    perror("Error: Failed to open file");
        return;
    }

    // Read the file content and send it to the client
    while ((read_bytes = read(file_descriptor, file_read_buffer, sizeof(file_read_buffer))) > 0) {
        send(client_socket, file_read_buffer, read_bytes, 0);
        if (read_bytes < sizeof(file_read_buffer)) {
            break; // End of file reached
        }
    }

    close(file_descriptor);
    sleep(2); // Small delay of 2 seconds to ensure the file transmission is complete

    snprintf(download_response, sizeof(download_response), "File %s downloaded successfully\n", file_name);
    send(client_socket, download_response, strlen(download_response), 0); // Notify client of successful download
}


void handle_remove_file(int client_socket, char *file_name) {
    char server_response[BUFFER_SIZE];   // Response to be sent back to the client
    char full_file_path[BUFFER_SIZE];    // Full path to the file to be removed

    // Construct the full path to the file
    snprintf(full_file_path, sizeof(full_file_path), "%s/stext/%s", valid_home_dir(), file_name);

    // Remove the file at the specified path
    if (remove(full_file_path) == 0) {
        // Notify client of successful deletion
        snprintf(server_response, sizeof(server_response), "File %s deleted successfully.\n", file_name);
    } else {
        // Notify client of failure to delete
        snprintf(server_response, sizeof(server_response), "Error: Unable to delete file %s.\n", file_name);
    }

    // Send the response to the client
    send(client_socket, server_response, strlen(server_response), 0);
}

void handle_create_tar(int client_socket, char *file_extension) {
    char tar_command[BUFFER_SIZE];    // Command to create tar file
    char client_message[BUFFER_SIZE]; // Message to be sent to the client
    FILE *tar_file_pointer;

    // Check if the file extension is supported
    if (strcmp(file_extension, ".txt") == 0) {
        // Construct the command to find .txt files and create a tar archive
        snprintf(tar_command, sizeof(tar_command), "find ~/stext -name '*.txt' | tar -cvf txt_list.tar -T -");
        system(tar_command); // Execute the command

        // Open the tar file to check if it was created successfully
        tar_file_pointer = fopen("txt_list.tar", "rb");
        if (tar_file_pointer) {
            fclose(tar_file_pointer); // Close the file if it was opened successfully
            snprintf(client_message, sizeof(client_message), "Tar file for %s files created successfully.\n", file_extension);
        } else {
            snprintf(client_message, sizeof(client_message), "Error: Failed to create tar file for %s files.\n", file_extension);
        }
    } else {
        // If the file extension is unsupported, notify the client
        snprintf(client_message, sizeof(client_message), "Error: Unsupported file type %s\n", file_extension);
    }

    // Send the response to the client
    send(client_socket, client_message, strlen(client_message), 0);
}

// Function to handle the display of .txt files in a specified directory
void handle_display(int client_socket, char *pathname) {
    char command[BUFFER_SIZE];                  // Buffer to hold the shell command string
    char buffer[BUFFER_SIZE];                   // Buffer to store output data read from the command execution
    FILE *pipe;                                 // File pointer for the pipe used to capture command output

    // Constructing a shell command to find all .txt files in the specified directory -maxdepth 1 limits the search to the specified directory (not recursive)
    // -exec basename {} \\; ensures that only the filenames (without path) are returned
    snprintf(command, sizeof(command), "find %s/stext/%s -maxdepth 1 -name '*.txt' -exec basename {} \\;", valid_home_dir(), pathname);

    // Opening a pipe to execute the constructed shell command and read its output
    pipe = popen(command, "r");
    if (pipe != NULL) {
        int files_found = 0;            // Flag to indicate whether any .txt files were found
        // Reading file names and send to client
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            // Send the filename to the client through the socket
            send(client_socket, buffer, strlen(buffer), 0);
            files_found = 1;
        }
        pclose(pipe);

        // If no files are found, send a message indicating no files available
        if (!files_found) {
            char no_files_msg[] = "No .txt files found in the specified directory.\n";
            send(client_socket, no_files_msg, strlen(no_files_msg), 0);
        }
    } else {
        perror("popen failed");
        // Notify the client that an error occurred while executing the command
        char error_msg[] = "Error executing find command.\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }
}