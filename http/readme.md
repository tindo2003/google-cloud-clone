## Features

- **Load Balancer**:
  - Listens on a public port (8080) for incoming client connections.
  - Monitors frontend servers by receiving periodic heartbeat messages on a designated port (9000).
  - Redirects clients to the available frontend server with an HTTP 307 Temporary Redirect response.

- **Frontend Server (FE Server)**:
  - Listens on a specified port (e.g., 5000, 5001, etc.) for redirected client requests from the load balancer.
  - Sends an initial message to the load balancer with its IP address and port.
  - Periodically sends heartbeat messages to the load balancer, indicating its active status.

## Configuration

- A configuration file `servers.txt` contains the list of potential frontend server addresses, each on a new line, in the format `IP:PORT` (e.g., `127.0.0.1:5000`).
- The frontend server uses a specified index to bind to the address listed on that line.


## How to Run

1. **Configure Frontend Servers**  
   Open `fe_servers.txt` and list each potential frontend (FE) server's address and port on a new line. For example:

127.0.0.1:5000 

127.0.0.1:5001 

127.0.0.1:5002


2. **Start the Load Balancer**  
Run the load balancer to listen for incoming client connections:

```bash
./load_balancer
```


3. **Start Frontend Servers**
    Each frontend server requires its configuration file line index as an argument:

```bash
./frontend_server 1
./frontend_server 2
```


## Template generator guide ##
Main functionality
1) If else 
2) For loop 
3) Variable substitution 
be careful of white space!