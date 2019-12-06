# Using nginx server
## Install nginx on the Raspberry Pi
nginx acts as the server that manages the connections and serves the streams that are created by libav/ffmpeg. 
Instructions to build & install nginx can be found [here](https://github.com/arut/nginx-rtmp-module/wiki/Getting-started-with-nginx-rtmp). 

After nginx is installed some of the files in this folder need to be copied to the Raspberry Pi for streaming. You can copy these by
```
sudo cp nginx.conf /usr/local/nginx/conf/nginx.conf
sudo cp stream.supervisor.conf /etc/supervisor/conf.d/stream.supervisor.conf
cp stream.sh /home/pi/stream.sh
```

The server will auto-start at boot-time. You can also stop/start the server by
```
sudo service supervisor stop
sudo service supervisor start
```

## Testing locally
We can also use docker to run nginx locally for testing, try `./run.sh -s <stream_folder>`.
