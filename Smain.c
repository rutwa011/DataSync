#include <stdio.h>      // Standard input/output operations
#include <stdlib.h>     // General purpose functions, including memory allocation
#include <string.h>     // String handling functions
#include <unistd.h>     // UNIX standard function definitions
#include <arpa/inet.h>  // Definitions for internet operations
#include <sys/types.h>  // Various data type definitions
#include <sys/socket.h> // Socket definitions and functions
#include <dirent.h>     // Directory entry operations
#include <sys/wait.h>   // Declarations for waiting
#include <sys/stat.h>   // File status and information
#include <fcntl.h>      // File control options
#include <errno.h>      // System error numbers

#define PORT 6009
#define SERVER_IP "127.0.1.4"
#define BUFFER_SIZE 1024
#define SPDF_PORT 6011
#define STEXT_PORT 6012
#define TEXT_ADDRESS "127.0.1.6"
#define PDF_ADDRESS "127.0.1.7"

const char *valid_home_dir()
{
    return getenv("HOME"); //Function to define the HOME directory which other functions will use as Path variable
}

int create_dir_if_new(const char *directory_path)
{
    char temp_path[BUFFER_SIZE]; // An array to store a temporary copy of the path
    char *sub_path = NULL; //  A pointer that will be used to traverse the path
    size_t path_length; // Variable to store the length of a path string

    // Copy the provided path into a temporary buffer
    snprintf(temp_path, sizeof(temp_path), "%s", directory_path); // copies the input directory_path into temp_path, ensuring it doesn't exceed the buffer size
    path_length = strlen(temp_path); // calculate the length of the string and stores in the path_length

    // Remove trailing slash if present by replacing it with null charachter
    if (temp_path[path_length - 1] == '/')
    {
        temp_path[path_length - 1] = '\0';
    }

    // Iterate over the path, creating directories as needed
    for (sub_path = temp_path + 1; *sub_path; sub_path++) // start iterating over the temp_string, strating from the second charachter
    {
        if (*sub_path == '/') // check if the current path is a slash
        {
            *sub_path = '\0'; // Temporarily terminate the string to create a directory
            // attempts to create a directory with permissions 0775
            // if the directory already exists then skip the error
            if (mkdir(temp_path, 0755) != 0 && errno != EEXIST)
            {
                perror("Could not create directory");
                return -1;
            }
            *sub_path = '/'; // Restore the slash after directory creation
        }
    }

    // Create the final directory in the path
    if (mkdir(temp_path, 0755) != 0 && errno != EEXIST)
    {
        perror("Failed to create directory");
        return -1;
    }

    return 0;
}

//Declaring functions beforehand and then working on them later in the code by defining them in required places
void process_client_request(int client_socket);
void process_uploaded_file(int client_socket, char *filename, char *destination, char *buffer);
void manage_file_download(int client_socket, char *filename, char *command);
void remove_file(int client_socket, char *filename, char *buffer);
void handle_dtar(int client_sock, char *filetype);
void handle_display(int client_sock, char *pathname);
int establish_connection(const char *ip_address, int port_number, int *socket_fd);

int main()
{
    int server_socket, client_socket; // declaring file descriptors for client and server
    struct sockaddr_in server_address, client_address; // define structure for server address and client address
    socklen_t client_address_len = sizeof(client_address); // setting  the length of the client address structure

    // Creating a socket for the server
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) // check if the socket fails or not
    {
        perror("Failed to create socket");
        exit(EXIT_FAILURE);
    }

    // Defining the server address and port
    server_address.sin_family = AF_INET; // setting the address family to AF_INET
    server_address.sin_addr.s_addr = INADDR_ANY;  // Listen on all available interfaces (127.0.0.1)
    server_address.sin_port = htons(PORT);        // Use the predefined port

    // Binding the server socket to the address and port
    if (bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Bind operation failed");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Setting the server socket to listen for incoming connections
    if (listen(server_socket, 3) < 0)
    {
        perror("Failed to listen on socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    printf("Smain server is listening on port %d...\n", PORT);

    // Main loop: accept incoming client connections
    while ((client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len)) >= 0)
    {
        printf("A new client has connected to the Smain server\n");

        // Creating a new process to handle the client connection
        if (fork() == 0)
        {
            // Child process: handle the client
            close(server_socket);                   // Closing the listening socket in the child process
            process_client_request(client_socket);  // Processing the client's requests
            close(client_socket);                   // Closing the client socket after processing
            exit(0);                                // Exiting the child process
        }
        else
        {
            // Parent process: close the client socket and continue listening for new connections
            close(client_socket);
        }
    }

    // Handle the case where accepting a connection fails
    if (client_socket < 0)
    {
        perror("Failed to accept connection");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Close the server socket before exiting the program
    close(server_socket);
    return 0;
}


void process_client_request(int client_socket)
{
    char buffer[BUFFER_SIZE]; // buffer to store the data recieved
    char command[BUFFER_SIZE]; // buffer to store the command entered by the user
    char argument1[BUFFER_SIZE], argument2[BUFFER_SIZE]; // buffer to store the command arguments

    // infinite loop to continuoulsy handle the input and processing
    while (1)
    {
        // clear the contents of the buffer by setting the elements to 0
        for (int i = 0; i < BUFFER_SIZE; i++)
        {
            buffer[i] = 0;
            command[i] = 0;
            argument1[i] = 0;
            argument2[i] = 0;
        }

        // Receiving a command from the client
        // Receive data from the client and store it in buffer
        // recv() reads up to BUFFER_SIZE - 1 bytes from the client_socket
        // and returns the number of bytes received or -1 if an error occurs
        int received_length = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (received_length <= 0)
        {
            if (received_length == 0)
            {
                printf("Client has disconnected\n");
                printf("Waiting for new connection request \n");
            }
            else
            {
                perror("Failed to receive data");
            }
            break;
        }

        // Null-terminate the buffer to safely handle it as a string
        buffer[received_length] = '\0';

        // Parse the received command and its arguments
        sscanf(buffer, "%s %s %s", command, argument1, argument2);

        printf("Command received: %s\n", command);

        // Handle the command based on the parsed input
        if (strcmp(command, "ufile") == 0)
        {
            process_uploaded_file(client_socket, argument1, argument2, buffer);         // to handle the ufile command
        }
        else if (strcmp(command, "dfile") == 0)
        {
            manage_file_download(client_socket, argument1, buffer);                     // to handle the dfile command
        }
        else if (strcmp(command, "rmfile") == 0)
        {
            remove_file(client_socket, argument1, buffer);                              // to handle the rmfile command
        }

        else if (strcmp(command, "dtar") == 0)
        {
            handle_dtar(client_socket, argument1);                                      // to handle the dtar command
        }

        else if (strcmp(command, "display") == 0)
        {
            handle_display(client_socket, argument1);                                   // to handle the display command
        }

        else
        {
            // Sending an error message for unrecognized commands
            char *error_message = "Invalid command\n";
            send(client_socket, error_message, strlen(error_message), 0);
        }
    }
}


void process_uploaded_file(int client_socket, char *filename, char *destination, char *buffer)
{
    char path[BUFFER_SIZE];             // Path to store the full file path
    char dest_path[BUFFER_SIZE];        // Path to store the destination directory path
    FILE *file_ptr;                     // File pointer for handling the file
    char server_response[BUFFER_SIZE];  // Buffer for sending responses to the client
    char file_data[BUFFER_SIZE];        // Buffer for storing the received file data
    int received_bytes;                 // Variable to store the number of bytes received

    if (strstr(filename, ".c") != NULL)
    {
        // Construct the destination path
        snprintf(dest_path, sizeof(dest_path), "%s/smain/%s", valid_home_dir(), destination);
        // Ensure the destination directory exists
        if (create_dir_if_new(dest_path) != 0)
        {
            return;
        }

        // Construct the full file path for the uploaded file
        snprintf(path, sizeof(path), "%s/smain/%s/%s", valid_home_dir(), destination, filename);
        // Open the file for writing
        file_ptr = fopen(path, "wb");
        if (file_ptr == NULL)
        {
            snprintf(server_response, sizeof(server_response), "Could not open file %s for writing\n", path);
            send(client_socket, server_response, strlen(server_response), 0);
            return;
        }

        // Receive data from the client
        printf("Receiving file: %s\n", path);
        while ((received_bytes = recv(client_socket, file_data, sizeof(file_data), 0)) > 0)
        {
            // Write the received data to the file
            fwrite(file_data, 1, received_bytes, file_ptr);

            if (received_bytes < sizeof(file_data))
            {
                printf("File scanned completely, copying to the Main server directory from the Client server\n");
                break;
            }
        }

        // Notifying the client that the file was uploaded successfully
        snprintf(server_response, sizeof(server_response), "File %s uploaded successfully\n", filename);
        send(client_socket, server_response, strlen(server_response), 0);
    }

    // Checking if the file is a text file
    else if (strstr(filename, ".txt") != NULL)
    {
        int stext_socket;

        // Establishing a connection to the Stext server
        establish_connection(TEXT_ADDRESS, STEXT_PORT, &stext_socket);
        // Sending the initial buffer to the Stext server
        if (send(stext_socket, buffer, BUFFER_SIZE, 0) == -1)
        {
            snprintf(server_response, sizeof(server_response), "Failed to establish socket connection for text files", filename);
            send(client_socket, server_response, strlen(server_response), 0);
        }

        // Forwarding the file data to the Stext server
        while ((received_bytes = recv(client_socket, file_data, sizeof(file_data), 0)) > 0)
        {
            send(stext_socket, file_data, received_bytes, 0);
            if (received_bytes < sizeof(file_data))
            {
                break;
            }
        }
        // Receive the response from the Stext server
        int length = recv(stext_socket, server_response, sizeof(server_response) - 1, 0);

        // Forward the response to the client
        send(client_socket, server_response, length, 0);
        close(stext_socket);
    }
    // Checking if the file is a PDF file
    else if (strstr(filename, ".pdf") != NULL)
    {
        int spdf_socket;

        // Establish a connection to the Spdf server
        establish_connection(PDF_ADDRESS, SPDF_PORT, &spdf_socket);
        if (send(spdf_socket, buffer, BUFFER_SIZE, 0) == -1)
        {
            snprintf(server_response, sizeof(server_response), "Failed to establish socket connection for PDF files", filename);
            send(client_socket, server_response, strlen(server_response), 0);
        }

        while ((received_bytes = recv(client_socket, file_data, sizeof(file_data), 0)) > 0)
        {
            send(spdf_socket, file_data, received_bytes, 0);
            if (received_bytes < sizeof(file_data))
            {
                break;
            }
        }

        // Receive the response from the Spdf server
        int length = recv(spdf_socket, server_response, sizeof(server_response) - 1, 0);
        // Forward the response to the client
        send(client_socket, server_response, length, 0);
        close(spdf_socket);
    }
    else
    {
        snprintf(server_response, sizeof(server_response), "File type %s is not supported.\n", filename);
        send(client_socket, server_response, strlen(server_response), 0);
    }
}


void manage_file_download(int client_socket, char *filename, char *command)
{
    char buffer[BUFFER_SIZE];           // Buffer for storing data to send or receive
    char file_path[BUFFER_SIZE];        // Path to store the full file path
    int file_descriptor;                // File descriptor for the file to be read
    int bytes_read;                     // Variable to store the number of bytes read
    char response[BUFFER_SIZE];         // Buffer for sending responses to the client

    // check if the file contains a .c extension
    if (strstr(filename, ".c") != NULL)
    {
        // Construct the full file path for the requested file
        // Construct the full file path by appending the "smain" directory and the filename
        // valid_home_dir() is assumed to return the home directory path
        snprintf(file_path, sizeof(file_path), "%s/smain/%s", valid_home_dir(), filename);
        printf("File: %s\n", file_path);

        file_descriptor = open(file_path, O_RDONLY); // attempts to open the file in realy only mode

        // Check if the file was successfully opened
        if (file_descriptor < 0)
        {
            perror("Error opening file");
            return;
        }

        // Read the file data and send it to the client
        // Continuously read data from the file descriptor into the buffer
        // Read up to sizeof(buffer) bytes from the file_descriptor
        while ((bytes_read = read(file_descriptor, buffer, sizeof(buffer))) >= 0)
        {
            // If no more data is read (bytes_read == 0), it indicates the end of the file
            if (bytes_read == 0)
            {
                send(client_socket, buffer, 1, 0);      // Sending empty buffer to indicate end of file
            }
            else
            {
                send(client_socket, buffer, bytes_read, 0); // send the raed data to the client
            }
            // If the number of bytes read is less than the buffer size, it means end of file is reached
            if (bytes_read < sizeof(buffer))
            {
                break; // End of file reached
            }
        }

        close(file_descriptor);
        // Delay to ensure that all data is sent
        sleep(2);

        // Notify the client that the file was downloaded successfully
        snprintf(response, sizeof(response), "File %s downloaded successfully\n", filename);
        send(client_socket, response, strlen(response), 0);
    }

    // Handling .txt and .pdf files by fetching from Spdf or Stext server
    else if (strstr(filename, ".txt") != NULL)
    {
        int text_socket;

        // establish connection to a text server at TEXT_ADD and STEXT_PORT
        // Pass the file descriptor for the text server socket to text_socket
        establish_connection(TEXT_ADDRESS, STEXT_PORT, &text_socket);
        send(text_socket, command, strlen(command), 0); // Sending the command to the text server to request the file or perform an operation
        
        // continuously send the data from the text server to the client
        while ((bytes_read = recv(text_socket, buffer, BUFFER_SIZE, 0)) >= 0)
        {
            send(client_socket, buffer, bytes_read, 0); // sending the received data to the client
            // if there are fewer bytes which indicates the end of the file or data transfer
            if (bytes_read < sizeof(buffer))
            {
                break; // End of file
            }
        }
        int length = recv(text_socket, response, sizeof(response), 0); // receive the final responce from the text server
        send(client_socket, response, strlen(response), 0); // send the final responce to the client
        close(text_socket); // closing the connection with the text server
    }
    // checking for the filename containing .pdf extension
    else if (strstr(filename, ".pdf") != NULL)
    {
        int pdf_socket;
        // Establish a connection to the PDF server at PDF_ADDRESS and SPDF_PORT
        // Pass the file descriptor for the PDF server socket to pdf_socket

        establish_connection(PDF_ADDRESS, SPDF_PORT, &pdf_socket); // Send the command to the PDF server to request the file or perform an operation
        send(pdf_socket, command, strlen(command), 0); // Send the command to the PDF server to request the file or perform an operation
        
        // continuously receive data from the PDF server and send it to the client
        while ((bytes_read = recv(pdf_socket, buffer, BUFFER_SIZE, 0)) >= 0)
        {
            send(client_socket, buffer, bytes_read, 0); // send the received data to the client
            // If fewer bytes than the buffer size are read, it indicates end of file or data transfer
            if (bytes_read < sizeof(buffer))
            {
                break; // End of file
            }
        }

        int length = recv(pdf_socket, response, sizeof(response), 0);  // Receive the final response or status from the PDF server
        // Send the final response to the client
        // Send the entire response buffer, even if it's not null-terminated
        send(client_socket, response, sizeof(response), 0);
        close(pdf_socket); // close the connection for the PDF server
    }
    else
    {
        snprintf(response, sizeof(response), "File type %s is not supported.\n", filename);
        send(client_socket, response, strlen(response), 0);
    }
}


void remove_file(int client_socket, char *filename, char *buffer)
{
    char response[BUFFER_SIZE];                 // Response buffer to store and send messages back to the client
        // Checking if the file to be removed is a C source file (".c")
        if (strstr(filename, ".c") != NULL)
    {
        char path[BUFFER_SIZE];

        // Constructing the full path to the file in the 'smain' directory
        snprintf(path, sizeof(path), "%s/smain/%s", valid_home_dir(), filename);
        remove(path);           // Remove the file from the server's filesystem
        snprintf(response, sizeof(response), "File %s deleted successfully.\n", filename);
        send(client_socket, response, strlen(response), 0);     // Send the success message back to the client
    }

    // Request removal from Spdf or Stext if the file is .pdf or .txt
    if (strstr(filename, ".txt") != NULL)
    {
        int text_socket;

        // Establish a connection to the Stext server using predefined address and port
        establish_connection(TEXT_ADDRESS, STEXT_PORT, &text_socket);
        // Send the request to remove the .txt file to the Stext server
        send(text_socket, buffer, strlen(buffer), 0);
        int length = recv(text_socket, response, BUFFER_SIZE, 0);
        // Forward the response from the Stext server to the client
        send(client_socket, response, strlen(response), 0);
        close(text_socket);
    }

    // Handling the removal of .pdf files from the Spdf server
    else if (strstr(filename, ".pdf") != NULL)
    {
        int pdf_socket;

        // Establishing a connection to the Spdf server using predefined address and port
        establish_connection(PDF_ADDRESS, SPDF_PORT, &pdf_socket);
        send(pdf_socket, buffer, strlen(buffer), 0);
        // Receiving the response from the Spdf server after processing the request
        int length = recv(pdf_socket, response, BUFFER_SIZE, 0);
        send(client_socket, response, strlen(response), 0);
        close(pdf_socket);
    }
}


void handle_dtar(int client_sock, char *filetype)
{
    char buffer[BUFFER_SIZE];   // Buffer for storing data to send or receive
    FILE *tar_file;             // File pointer to handle tar files locally
    size_t file_size;           // Variable to hold the size of the file

    if (strcmp(filetype, ".c") == 0)
    {
        // Handling .c files locally on Smain server

        // Create a tar archive of all .c files in the ~/smain directory
        // The system function executes the shell command to find all .c files and archive them into a tar file
        system("find ~/smain -name '*.c' | tar -cvf c_list.tar -T -");
        tar_file = fopen("c_list.tar", "rb");
    }
    // Checking if the file type is ".pdf"
    else if (strcmp(filetype, ".pdf") == 0)
    {
        // Handle .pdf files on Spdf server
        int spdf_sock;
        establish_connection(PDF_ADDRESS, SPDF_PORT, &spdf_sock);
        // Send command to Spdf server to create the tar file
        send(spdf_sock, "dtar .pdf", strlen("dtar .pdf"), 0);

        close(spdf_sock);
    }
    // Checking if the file type is ".txt"
    else if (strcmp(filetype, ".txt") == 0)
    {
        // Handle .txt files on Stext server
        int stext_sock;

        // Establish a connection to the Stext server using its address and port number
        establish_connection(TEXT_ADDRESS, STEXT_PORT, &stext_sock);

        // Send command to Stext server to create the tar file
        send(stext_sock, "dtar .txt", strlen("dtar .txt"), 0);

        close(stext_sock);
    }
    else        // If the file type is not supported
    {
        char *msg = "Unsupported file type\n";

        // Send the error message to the client
        send(client_sock, msg, strlen(msg), 0);
        return;
    }

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), "File uploaded successfully\n");

    // Send the success message to the client
    send(client_sock, response, strlen(response), 0);
}

void handle_display(int client_sock, char *pathname) {
    // Buffers to store lists of different file types
    char c_files_list[BUFFER_SIZE] = "";
    char pdf_files_list[BUFFER_SIZE] = "";
    char txt_files_list[BUFFER_SIZE] = "";
    char final_list[3 * BUFFER_SIZE] = ""; // Buffer to store the final combined list of all file types
    char command[BUFFER_SIZE];  // Buffer to hold commands or file paths

    // Retrieving the list of .c files in the specified local directory (within smain) -maxdepth 1 ensures we only search the specified directory and not subdirectories
    // -exec basename {} \; strips the directory path, leaving just the filenames
    snprintf(command, sizeof(command), "find %s/smain/%s -maxdepth 1 -name '*.c' -exec basename {} \\;", valid_home_dir(), pathname);
    FILE *fp = popen(command, "r"); // Execute the command using popen and open a pipe to read its output
    if (fp != NULL) {
        // Reading the output of the find command line by line and append it to the c_files_list buffer
        while (fgets(command, sizeof(command), fp) != NULL) {
            strcat(c_files_list, command);
        }
        pclose(fp);
    }

    // Request the list of .pdf files from Spdf
    int spdf_socket;
    if (establish_connection(PDF_ADDRESS, SPDF_PORT, &spdf_socket) == 0)
        {
        // Constructing the display command for the server to list .pdf files
        snprintf(command, sizeof(command), "display %s", pathname);
        if (send(spdf_socket, command, strlen(command), 0) >= 0) {
        // Receiving the list of .pdf files from the Spdf server
        recv(spdf_socket, pdf_files_list, sizeof(pdf_files_list) - 1, 0);
        }
        close(spdf_socket);
    }

    // Request the list of .txt files from Stext
    int stext_socket;
    if (establish_connection(TEXT_ADDRESS, STEXT_PORT, &stext_socket) == 0)
        {
                // Constructing the display command for the server to list .txt files
                snprintf(command, sizeof(command), "display %s", pathname);
        if (send(stext_socket, command, strlen(command), 0) >= 0) {
        // Receiving the list of .txt files from the Stext server
            recv(stext_socket, txt_files_list, sizeof(txt_files_list) - 1, 0);
        }
        close(stext_socket);
    }

    // Combining the lists of .c, .pdf, and .txt files into a single list
    strcat(final_list, c_files_list);
    strcat(final_list, pdf_files_list);
    strcat(final_list, txt_files_list);

    // Send the final list to the client
    if (strlen(final_list) > 0) {
        // If files are found, send the complete list to the client
        send(client_sock, final_list, strlen(final_list), 0);
    } else {
        // If no files are found, send a message indicating no files available
        char no_files_msg[] = "No files available in the specified directory.\n";
        send(client_sock, no_files_msg, strlen(no_files_msg), 0);
    }
}

int establish_connection(const char *ip_address, int port_number, int *socket_fd)
{
    // Define a sockaddr_in structure to store the server's address information
    struct sockaddr_in server_address;

    // Creating a socket using the IPv4 address family (AF_INET) and TCP (SOCK_STREAM)
    // The third parameter is 0, which allows the OS to select the appropriate protocol
    *socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    // Checking if the socket was successfully created
    // If the socket file descriptor is negative, an error occurred
    if (*socket_fd < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
        return -1;
    }

    // Specifying the address family as IPv4
    server_address.sin_family = AF_INET;
    // Converting the port number from host byte order to network byte order
    server_address.sin_port = htons(port_number);

    if (inet_pton(AF_INET, ip_address, &server_address.sin_addr) <= 0)
    {
        perror("Address conversion failed");
        exit(EXIT_FAILURE);
        return -1;
    }

    // Attempting to connect to the server using the socket and the server address structure
    // The connect() function establishes a connection to the server
    if (connect(*socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Failed to connect to server");
        exit(EXIT_FAILURE);
    }
    return 0;
}