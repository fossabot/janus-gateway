sed -i "s@^backend.*@backend = $JANUS_EVENT_LISTENER@g" /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
/etc/init.d/cron restart
ulimit -c unlimited
janus
