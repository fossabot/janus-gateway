sed -i "s@http:\/\/localhost:3050\/events@$JANUS_EVENT_LISTENER@g" /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
/etc/init.d/nginx restart
/etc/init.d/cron restart
janus