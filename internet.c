#include <stdio.h>      // Include standard input/output library for functions like printf.
#include <stdlib.h>     // Include standard library for general purpose functions.
#include <string.h>     // Include string library for string manipulation functions.
#include <unistd.h>     // Include unistd.h for POSIX operating system API, like close().
#include <arpa/inet.h>  // Include arpa/inet.h for definitions for internet operations, like inet_pton().
#include <netinet/in.h> // Include netinet/in.h for internet address family structures.
#include <sys/socket.h> // Include sys/socket.h for socket programming functions.
#include <sys/time.h>   // Include sys/time.h for time-related structures like timeval.

// Function to test for an active internet connection.
int test_internet_connection() {
    const char *host = "8.8.8.8"; // Define the IP address of a reliable host to test against (Google's public DNS).
    const int port = 53;          // Define the port to connect to (port 53 is for DNS).

    int sock = socket(AF_INET, SOCK_STREAM, 0); // Create a new socket (AF_INET for IPv4, SOCK_STREAM for TCP).
    if (sock < 0) {                             // Check if socket creation failed.
        perror("Socket creation failed");       // Print an error message to stderr.
        return 0;                               // Return 0 (false) to indicate no connection.
    }

    struct sockaddr_in server;                  // Declare a structure to hold the server's address information.
    server.sin_family = AF_INET;                // Set the address family to IPv4.
    server.sin_port = htons(port);              // Set the port number, converting it to network byte order.
    inet_pton(AF_INET, host, &server.sin_addr); // Convert the IP address string to a binary format and store it in the struct.

    // 5 second timeout
    struct timeval timeout;                                                              // Declare a structure to define a timeout period.
    timeout.tv_sec = 5;                                                                  // Set the seconds part of the timeout to 5.
    timeout.tv_usec = 0;                                                                 // Set the microseconds part of the timeout to 0.
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)); // Set the receive timeout option for the socket.
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)); // Set the send timeout option for the socket.

    int result = connect(sock, (struct sockaddr *)&server, sizeof(server)); // Attempt to connect to the server.
    close(sock);                                                            // Close the socket, releasing the file descriptor.

    return result == 0; // Return 1 (true) if connect() succeeded (returned 0), otherwise return 0 (false).
}

// The main function of the program.
int main() {
    if (test_internet_connection()) { // Call the connection test function and check its result.
        printf("✅ Internet connection is available!\n"); // If it returns true, print a success message.
    } else {                                            // If the test fails.
        printf("❌ No internet connection.\n");          // Print a failure message.
    }

    return 0; // Exit
