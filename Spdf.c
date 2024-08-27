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

#define PORT 6011
#define BUFFER_SIZE 1024
#define ADDRESS "127.0.1.7"

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

    // Traverse the path and create directories one by one
    for (path_position = path_copy + 1; *path_position; path_position++) {
        if (*path_position == '/') {
            *path_position = '\0'; // Temporarily terminate the string to create a directory
            if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
                // Handle errors during directory creation
                perror("Error creating directory");
                return -1;
            }
            *path_position = '/'; // Restore the slash after creating the directory
        }
    }

    // Create the final directory in the path
    if (mkdir(path_copy, 0755) != 0 && errno != EEXIST) {
        perror("Error creating final directory");
        return -1;
    }

    return 0; // Return 0 to indicate success
}

void process_client(int sock_client);
void process_upload(int sock_client, char *file_name, char *path_dest, char *recv_buffer);
void process_download(int sock_client, char *file_name);
void handle_remove_file(int client_socket, char *file_name);
void handle_create_tar(int client_socket, char *file_extension);
void handle_display(int client_socket, char *pathname);

int main()
{
    int sock_server, sock_client; // Declare a socket descriptor for the server socket, client socket
    struct sockaddr_in addr_server, addr_client; // Define the server address structure to hold server address information
    socklen_t len_addr = sizeof(addr_client); // Define the client address structure to hold client address information

    // Create a socket for the server
    if ((sock_server = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    addr_server.sin_family = AF_INET; // Set the address family to AF_INET for IPv4 addresses
    // Set the port number for the server address, converting from host byte order to network byte order
    // htons() ensures the port number is in the correct byte order for network communication
    addr_server.sin_port = htons(PORT);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, ADDRESS, &addr_server.sin_addr) <= 0)
    {
        perror("Invalid IP address");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the specified address and port
    if (bind(sock_server, (struct sockaddr *)&addr_server, sizeof(addr_server)) < 0)
    {
        perror("Binding failed");
        close(sock_server);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(sock_server, 3) < 0)
    {
        perror("Failed to listen");
        close(sock_server);
        exit(EXIT_FAILURE);
    }

    printf("PDF server running and listening on port %d\n", PORT);

    // Accept and handle incoming client connections
    while ((sock_client = accept(sock_server, (struct sockaddr *)&addr_client, &len_addr)) >= 0)
    {
        printf("Incoming client connection...\n");
        printf("Establishing connection...\n");

        // Fork a new process to handle the client connection that handles multiple clients
        if (fork() == 0) // child process
        {
            close(sock_server); // Close the server socket in the child process as it is not needed
            process_client(sock_client); // to handle the client requests
            close(sock_client);
            exit(0);
        }
        else
        {
            close(sock_client);
            wait(NULL); // Wait for the child process to terminate
        }
    }

    if (sock_client < 0)
    {
    //    perror("Failed to accept connection");
        close(sock_server);
        exit(EXIT_FAILURE);
    }

    close(sock_server);
    return 0;
}

void process_client(int sock_client)
{
    char recv_buffer[BUFFER_SIZE]; // Buffer to store the data received from the client
    char cmd[BUFFER_SIZE]; // Buffer to store the command received from the client
    char param1[BUFFER_SIZE], param2[BUFFER_SIZE]; // Buffer to store the first and second parameter for the command received from the client

    // Infinite loop to continuously handle client requests
    while (1)
    {
        // Clear buffers before processing a new client request
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            recv_buffer[i] = 0;
            cmd[i] = 0;
            param1[i] = 0;
            param2[i] = 0;
        }
        // Receive the command from the client
        if (recv(sock_client, recv_buffer, BUFFER_SIZE, 0) <= 0)
        {
        //    perror("Error in receiving data");
            break;  // Exit loop on receiving failure
        }

        // Parse the received command and arguments
        sscanf(recv_buffer, "%s %s %s", cmd, param1, param2);

        // Handle the command based on its type
        if (strcmp(cmd, "ufile") == 0)
        {
            process_upload(sock_client, param1, param2, recv_buffer); // to handle to ufile command
        }
        else if (strcmp(cmd, "dfile") == 0)
        {
            process_download(sock_client, param1); // to handle the dfile function
        }
        else if (strcmp(cmd, "rmfile") == 0)
        {
            handle_remove_file(sock_client, param1); // to handle the rmfile command
        }
        else if (strcmp(cmd, "dtar") == 0)
        {
            handle_create_tar(sock_client, param1); // to handle the dtar command
        }
        else if (strcmp(cmd, "display") == 0)
        {
           handle_display(sock_client, param1);  // to handle the display command
        }
        else
        {
            // Send an error message if the command is invalid
            const char *err_msg = "Command not recognized\n";
            send(sock_client, err_msg, strlen(err_msg), 0);
        }
    }
}

void process_upload(int sock_client, char *file_name, char *path_dest, char *recv_buffer)
{
    char full_file_path[BUFFER_SIZE];           // Buffer to hold the full path of the file to be uploaded
    FILE *file_pointer;                         // File pointer to manage file operations
    char response_buffer[BUFFER_SIZE];          // Buffer to hold responses sent back to the client
    char dest_dir_path[BUFFER_SIZE];            // Buffer to hold the path of the destination directory
    char recv_file_buffer[BUFFER_SIZE];         // Buffer to hold chunks of the file data received from the client
    int recv_bytes;                                                     // Variable to store the number of bytes received

    // Construct the full path for the destination directory where the file will be uploaded
    snprintf(dest_dir_path, sizeof(dest_dir_path), "%s/spdf/%s", valid_home_dir(), path_dest);

    // Create the directory structure if it doesn't exist
    if (create_dir_if_new(dest_dir_path) != 0)
    {
        return;                                 // Exit if the directory cannot be created
    }

    // Construct the full file path
    snprintf(full_file_path, sizeof(full_file_path), "%s/spdf/%s/%s", valid_home_dir(), path_dest, file_name);

    // Open the file for writing
    file_pointer = fopen(full_file_path, "wb");
    if (file_pointer == NULL)
    {
        // Send an error message to the client if the file cannot be opened
        snprintf(response_buffer, sizeof(response_buffer), "Unable to open file %s for writing\n", full_file_path);
        send(sock_client, response_buffer, strlen(response_buffer), 0);
        return;
    }

    printf("Starting to receive file: %s\n", full_file_path);   // Informing the server that the file reception is starting


    // Loop to receive file data from the client and write it to the file
    while ((recv_bytes = recv(sock_client, recv_file_buffer, sizeof(recv_file_buffer), 0)) > 0)
    {
        printf("The file has %d bytes\n", recv_bytes);          // Displaying the number of bytes received

        // Write the received data to the file
        fwrite(recv_file_buffer, 1, recv_bytes, file_pointer);
        if (recv_bytes < sizeof(recv_file_buffer))
        {
            printf("File transfer in progress from the Client to the Main server Directory..\n");
            break; // End of file
        }
    }

    // Send a success message to the client indicating that the file has been successfully uploaded
    snprintf(response_buffer, sizeof(response_buffer), "File %s successfully uploaded\n", file_name);
    send(sock_client, response_buffer, strlen(response_buffer), 0);
    fclose(file_pointer); // Close the file after the upload is complete
}

// Function to handle the download process from the server to the client
void process_download(int sock_client, char *file_name)
{
    char read_buffer[BUFFER_SIZE];              // Buffer to hold chunks of the file data to be sent to the client
    char file_full_path[BUFFER_SIZE];           // Buffer to hold the full path of the file to be downloaded
    int file_descriptor;                        // File descriptor for the file being read
    int bytes_read;                             // Variable to store the number of bytes read from the file
    char response_message[BUFFER_SIZE];         // Buffer to hold responses sent back to the client

    // Construct the full path of the file to be sent
    snprintf(file_full_path, sizeof(file_full_path), "%s/spdf/%s", valid_home_dir(), file_name);
    printf("Preparing to download file from: %s\n", file_full_path);            // Inform the server that the file download is starting

    // Open the file in read-only mode
    file_descriptor = open(file_full_path, O_RDONLY);
    if (file_descriptor < 0)
    {
    //    perror("Error opening file for reading");
        return;
    }

    // Read and send the file in chunks
    while ((bytes_read = read(file_descriptor, read_buffer, sizeof(read_buffer))) > 0)
    {
        // Sending the read data to the client
        send(sock_client, read_buffer, bytes_read, 0);

        // If the number of bytes read is less than the buffer size, it indicates the end of the file
        if (bytes_read < sizeof(read_buffer))
        {
            break; // End of file reached
        }
    }

    close(file_descriptor); // Close the file after sending
    sleep(3); // Delay to ensure client-side processing

    snprintf(response_message, sizeof(response_message), "File %s successfully downloaded\n", file_name);
    send(sock_client, response_message, strlen(response_message), 0);
}


void handle_remove_file(int client_socket, char *file_name) {
    char server_response[BUFFER_SIZE];   // Response to be sent back to the client
    char full_file_path[BUFFER_SIZE];    // Full path to the file to be removed

    // Construct the full path to the file
    snprintf(full_file_path, sizeof(full_file_path), "%s/spdf/%s", valid_home_dir(), file_name);

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
    if (strcmp(file_extension, ".pdf") == 0) {
        // Construct the command to find .pdf files and create a tar archive
        snprintf(tar_command, sizeof(tar_command), "find ~/spdf -name '*.pdf' | tar -cvf pdf_list.tar -T -");
        system(tar_command); // Execute the command

        // Open the tar file to check if it was created successfully
        tar_file_pointer = fopen("pdf_list.tar", "rb");
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

void handle_display(int client_socket, char *pathname) {
    char command[BUFFER_SIZE]; // buffer to store the command to be excecuted
    char buffer[BUFFER_SIZE]; // buffer to store the output of the executed command
    FILE *pipe; // file pointer to handle the pipe for reading command output

    // Command to ensure only filenames are returned
    snprintf(command, sizeof(command), "find %s/spdf/%s -maxdepth 1 -name '*.pdf' -exec basename {} \\;", valid_home_dir(), pathname);

    // Execute the command and open a pipe to read its output
    pipe = popen(command, "r");
    // check if the pipe was succesfully opened
    if (pipe != NULL) {
        int files_found = 0;
        // Read file names and send to client
        while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
            send(client_socket, buffer, strlen(buffer), 0);
            files_found = 1;
        }
        pclose(pipe);

        // If no files are found, send a message indicating no files available
        if (!files_found) {
            char no_files_msg[] = "No .pdf files found in the specified directory.\n";
            send(client_socket, no_files_msg, strlen(no_files_msg), 0);
        }
    } else {
        perror("popen failed");
        char error_msg[] = "Error executing find command.\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
    }
}