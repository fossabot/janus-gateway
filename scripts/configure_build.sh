## Janus cconfiguration
sed -i 's/;stun_server = stun.voip.eutelia.it/stun_server = stun.l.google.com/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;stun_port = 3478/stun_port = 19302/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;turn_server = myturnserver.com/turn_server = ${TURN_SERVER}/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;turn_port = 3478/turn_port = ${TURN_PORT}/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;turn_type = udp/turn_type = ${TURN_TYPE}/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;turn_user = myuser/turn_user = ${TURN_USER}/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;turn_pwd = mypassword/turn_pwd = ${TURN_PWD}/g' /usr/local/etc/janus/janus.cfg
sed -i 's/;debug_timestamps = yes/debug_timestamps = yes/g' /usr/local/etc/janus/janus.cfg
sed -i 's/; broadcast = yes/broadcast = yes/g' /usr/local/etc/janus/janus.cfg
sed -i 's/enabled = no/enabled = yes/g' /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
sed -i 's/events = all/events = plugins/g' /usr/local/etc/janus/janus.eventhandler.sampleevh.cfg
chmod +x ${JANUS_PATH}/scripts/run_janus.sh

## Config others
ln  -s ${RECORDING_PATH} ${JANUS_PATH}/html/recordings
echo "0  0-23/5 * * * root    find ${RECORDING_PATH}/ -type f -mmin +600 -delete" >> /etc/crontab
