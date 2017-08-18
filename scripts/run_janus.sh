sed -i "s@http:\/\/your.webserver.here\/and\/a\/path@$JANUS_DIRECTOR_CALLBACK@g" /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
/etc/init.d/nginx restart
/usr/local/bin/janus
