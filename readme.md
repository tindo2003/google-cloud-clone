# How to use the `run.sh` script to run our programs

Update the absolute path in the script to match your local environment.  

For example:

```bash
gnome-terminal --tab -- bash -c "cd <your-absolute-path>/backend && make clean && make coordinator && ./coordinator config 8006 9000; exec bash"
sleep 1
```
Navigate to your project root and run,
```bash
sudo bash ./run.sh
```

# Potential issues
If you encounter issue with the script, try running:
```bash
sudo apt install gnome-terminal
chmod +x run.sh
```

If you encounter issue with uuid, try running:
```bash
sudo apt-get install uuid-dev
```

# Notes
- Make sure to have empty backend/checkpoints, backend/logs, backend/error before running

- To add more backend servers, update the `/backend/config` file. To add more frontend servers, modify the `/http/fe_servers.txt` file. Additionally, ensure the `run.sh` script is updated to spawn the correct number of processes based on the changes in these files.

- Screenshots are included in the /screenshots folder
