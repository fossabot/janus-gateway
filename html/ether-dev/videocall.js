// We make use of this 'server' variable to provide the address of the
// REST Janus API. By default, in this example we assume that Janus is
// co-located with the web server hosting the HTML pages but listening
// on a different port (8088, the default for HTTP in Janus), which is
// why we make use of the 'window.location.hostname' base address. Since
// Janus can also do HTTPS, and considering we don't really want to make
// use of HTTP for Janus if your demos are served on HTTPS, we also rely
// on the 'window.location.protocol' prefix to build the variable, in
// particular to also change the port used to contact Janus (8088 for
// HTTP and 8089 for HTTPS, if enabled).
// In case you place Janus behind an Apache frontend (as we did on the
// online demos at http://janus.conf.meetecho.com) you can just use a
// relative path for the variable, e.g.:
//
// 		var server = "/janus";
//
// which will take care of this on its own.
//
//
// If you want to use the WebSockets frontend to Janus, instead, you'll
// have to pass a different kind of address, e.g.:
//
// 		var server = "ws://" + window.location.hostname + ":8188";
//
// Of course this assumes that support for WebSockets has been built in
// when compiling the gateway. WebSockets support has not been tested
// as much as the REST API, so handle with care!
//
//
// If you have multiple options available, and want to let the library
// autodetect the best way to contact your gateway (or pool of gateways),
// you can also pass an array of servers, e.g., to provide alternative
// means of access (e.g., try WebSockets first and, if that fails, fall
// back to plain HTTP) or just have failover servers:
//
//		var server = [
//			"ws://" + window.location.hostname + ":8188",
//			"/janus"
//		];
//
// This will tell the library to try connecting to each of the servers
// in the presented order. The first working server will be used for
// the whole session.
//
var server = null;
if(window.location.protocol === 'http:')
	server = "http://" + "ethermeet.etherlabs.io"+ "/janus-meet/janus";
else
	server = "https://" + "ethermeet.etherlabs.io" + "/janus-meet/janus";

var janus = null;
var sfutest = null;
var opaqueId = "videoroomtest-"+Janus.randomString(12);

var started = false;

var myusername = null;
var myroom = null;
var myid = null;
var mystream = null;
// We use this other ID just to map our subscriptions to us
var mypvtid = null;

var feeds = [];
var bitrateTimer = [];
var meetingId = null;
var userId	= null;
var startTime = null;
var currentProgressbarValue = null;
var currentCallTime = null;
var progressbarMaxVal = 720;
var maxCallTime = 60;
var progressbarRefreshInterval = 2;//sec
var timeLapsedRefreshInterval = 60;//sec
var currentMeetingInfo = null;
var maxProgressPerc = 95;

$(document).ready(function() {
	// Initialize the library (all console debuggers enabled)
	Janus.init({debug: "all", callback: function() {
		// Use a button to start the demo
		parseQueryParams()
		getCurrentMeetingInfo()
		window.onload = handleJanusCall;
		$("#registernow").click(handleJanusCall);
		$(".bootbox .btn-primary").click(function(){window.location.replace(window.location.origin)})
	}});
});


function getCurrentMeetingInfo(){
	if (currentMeetingInfo === null){
		$.ajax({
			type: "GET",
			url: "https://ether-staging-1553540497.us-east-1.elb.amazonaws.com:8080/v1/meetings/"+meetingId,
			// url: "http://localhost:8080/v1/meetings/"+meetingId,
			crossDomain: true,
			success: function(res){
				currentMeetingInfo = res
				console.log(currentMeetingInfo)
				populateFieldsMeetingFields()
			}
		})
	}
	return currentMeetingInfo
}

function populateFieldsMeetingFields(){
	currentCallTime = getCurrentCallTime()
	initProgressbar(currentCallTime)
	refreshTime(true)
	setInterval(progressTheBar, progressbarRefreshInterval*1000)
	setInterval(refreshTime, timeLapsedRefreshInterval*1000)
}

function getCurrentCallTime(){
	if (startTime === null || startTime === ""){
		startTime = getCurrentMeetingInfo().startedAt
		if (startTime === "0001-01-01T00:00:00Z"){
			startTime = (new Date()).toISOString()
		}
	}
	var startedAt = new Date(startTime)
	diff = Date.now()-startedAt
	return diff/(1000*60)
}

function refreshTime(init = false){
	if (!init)
		currentCallTime += timeLapsedRefreshInterval/60
	hr = Math.floor(currentCallTime/60)
	min = Math.floor(currentCallTime%60)
	$('.el-progress .progress-bar--time').html(formattedTime(hr,min))
}

function formattedTime(hr, min){
	if (hr < 10)
		hr = "0"+hr
	if (min < 10)
		min = "0"+min
	return hr+":"+min
}

function handleJanusCall() {
	if(started)
		return;
	started = true;
	// $(this).attr('disabled', true).unbind('click');
	// Make sure the browser supports WebRTC
	if(!Janus.isWebrtcSupported()) {
		bootbox.alert("No WebRTC support... ");
		return;
	}
	// Create session
	janus = new Janus(
		{
			server: server,
			success: function() {
				// Attach to video room test plugin
				janus.attach(
					{
						plugin: "janus.plugin.videoroom",
						opaqueId: opaqueId,
						success: function(pluginHandle) {
							$('#details').remove();
							sfutest = pluginHandle;
							Janus.log("Plugin attached! (" + sfutest.getPlugin() + ", id=" + sfutest.getId() + ")");
							Janus.log("  -- This is a publisher/manager");
							joinMeeting()

						},
						error: function(error) {
							$('#registernow').removeClass('hide').show();
							Janus.error("  -- Error attaching plugin...", error);
							bootbox.alert("Error attaching plugin... " + error);
						},
						consentDialog: function(on) {
							Janus.debug("Consent dialog should be " + (on ? "on" : "off") + " now");
							if(on) {
								// Darken screen and show hint
								$.blockUI({
									message: '<div><img src="assets/images/up_arrow.png" style="transform: rotate(270deg);"/></div>',
									css: {
										border: 'none',
										padding: '15px',
										backgroundColor: 'transparent',
										color: '#aaa',
										top: '10px',
										left: (navigator.mozGetUserMedia ? '-100px' : '300px')
									} });
							} else {
								// Restore screen
								$.unblockUI();
							}
						},
						mediaState: function(medium, on) {
							Janus.log("Janus " + (on ? "started" : "stopped") + " receiving our " + medium);
						},
						webrtcState: function(on) {
							Janus.log("Janus says our WebRTC PeerConnection is " + (on ? "up" : "down") + " now");
							$("#videos").unblock();
						},
						onmessage: function(msg, jsep) {
							Janus.debug(" ::: Got a message (publisher) :::");
							Janus.debug(JSON.stringify(msg));
							var event = msg["videoroom"];
							Janus.debug("Event: " + event);
							if(event != undefined && event != null) {
								if(event === "joined") {
									// Publisher/manager created, negotiate WebRTC and attach to existing feeds, if any
									$('#registernow').addClass('hide');
									$('#videojoin').removeClass('hide').show();
									myid = msg["id"];
									mypvtid = msg["private_id"];
									Janus.log("Successfully joined room " + msg["room"] + " with ID " + myid);
									publishOwnFeed(true);
									// Any new feed to attach to?
									if(msg["publishers"] !== undefined && msg["publishers"] !== null) {
										var list = msg["publishers"];
										Janus.debug("Got a list of available publishers/feeds:");
										Janus.debug(list);
										for(var f in list) {
											var id = list[f]["id"];
											var display = list[f]["display"];
											Janus.debug("  >> [" + id + "] " + display);
											newRemoteFeed(id, display)
										}
									}
								} else if(event === "destroyed") {
									// The room has been destroyed
									Janus.warn("The room has been destroyed!");
									bootbox.alert("The room has been destroyed", function() {
										window.location.replace(window.location.origin)
									});
								} else if(event === "event") {
									// Any new feed to attach to?
									if(msg["publishers"] !== undefined && msg["publishers"] !== null) {
										var list = msg["publishers"];
										Janus.debug("Got a list of available publishers/feeds:");
										Janus.debug(list);
										for(var f in list) {
											var id = list[f]["id"];
											var display = list[f]["display"];
											Janus.debug("  >> [" + id + "] " + display);
											newRemoteFeed(id, display)
										}
									} else if(msg["leaving"] !== undefined && msg["leaving"] !== null) {
										// One of the publishers has gone away?
										var leaving = msg["leaving"];
										Janus.log("Publisher left: " + leaving);
										var remoteFeed = null;
										for(var i=1; i<6; i++) {
											if(feeds[i] != null && feeds[i] != undefined && feeds[i].rfid == leaving) {
												remoteFeed = feeds[i];
												break;
											}
										}
										if(remoteFeed != null) {
											Janus.debug("Feed " + remoteFeed.rfid + " (" + remoteFeed.rfdisplay + ") has left the room, detaching");
											$('#remote'+remoteFeed.rfindex).empty().hide();
											$('#videoremote'+remoteFeed.rfindex).empty();
											feeds[remoteFeed.rfindex] = null;
											remoteFeed.detach();
										}
									} else if(msg["unpublished"] !== undefined && msg["unpublished"] !== null) {
										// One of the publishers has unpublished?
										var unpublished = msg["unpublished"];
										Janus.log("Publisher left: " + unpublished);
										if(unpublished === 'ok') {
											// That's us
											sfutest.hangup();
											return;
										}
										var remoteFeed = null;
										for(var i=1; i<6; i++) {
											if(feeds[i] != null && feeds[i] != undefined && feeds[i].rfid == unpublished) {
												remoteFeed = feeds[i];
												break;
											}
										}
										if(remoteFeed != null) {
											Janus.debug("Feed " + remoteFeed.rfid + " (" + remoteFeed.rfdisplay + ") has left the room, detaching");
											$('#remote'+remoteFeed.rfindex).empty().hide();
											$('#videoremote'+remoteFeed.rfindex).empty();
											feeds[remoteFeed.rfindex] = null;
											remoteFeed.detach();
										}
									} else if(msg["error"] !== undefined && msg["error"] !== null) {
										bootbox.alert(msg["error"]);
									}
								}
							}
							if(jsep !== undefined && jsep !== null) {
								Janus.debug("Handling SDP as well...");
								Janus.debug(jsep);
								sfutest.handleRemoteJsep({jsep: jsep});
							}
						},
						onlocalstream: function(stream) {
							Janus.debug(" ::: Got a local stream :::");
							mystream = stream;
							Janus.debug(JSON.stringify(stream));
							$('#videojoin').hide();
							$('#videos').removeClass('hide').show();
							if($('.myvideo').length === 0) {
								$('#videolocal').append('<video class="rounded centered myvideo" width="100%" height="100%" autoplay muted="muted" style="transform: rotateY(180Deg);"/>');
								$('#videolocal_side').append('<video class="rounded centered myvideo" width="100%" height="100%" autoplay muted="muted" style="transform: rotateY(180Deg);"/>');
								// Add a 'mute' button
								// $('#videolocal').append('<button class="btn btn-warning btn-xs" id="mute" style="position: absolute;bottom: 0px;left: 0px;margin: 41px;background: transparent;"><img class="audio" src="microphone-128.png" style="width: 23px;"/></button>')
								// $('#videolocal').append('<button class="btn btn-warning btn-xs" id="videomute" style="position: absolute;bottom: 0px;left: 60px;margin: 41px;background: transparent;"><img class="video" src="video-128.png" style="width: 23px;"/></button>');

								$('#mic').click(toggleMute);
								$('#camera').click(toggleVideo);
								// Add an 'unpublish' button
								// $('#videolocal').append('<button class="btn btn-warning btn-xs" id="unpublish" style="position: absolute; bottom: 0px; right: 0px; margin: 15px;">Unpublish</button>');
								$('#unpublish').click(unpublishOwnFeed);
								$('#videolocal').removeClass('hide').show()
								$('#videolocal .el-participants--item-name').html(myusername)
								$('#videolocal_side .el-participants--item-name').html(myusername)
							}
							Janus.attachMediaStream($('.myvideo').get(0), stream);
							Janus.attachMediaStream($('.myvideo').get(1), stream);
							$(".myvideo").get(0).muted = "muted";
							$(".myvideo").get(1).muted = "muted";
							$("#videos").block({
								message: '<b>Joining Ether Call ...</b>',
								css: {
									border: 'none',
									backgroundColor: 'transparent',
									color: 'white'
								}
							});
							var videoTracks = stream.getVideoTracks();
							if(videoTracks === null || videoTracks === undefined || videoTracks.length === 0) {
								// No webcam
								$('.myvideo').hide();
								$('#videolocal').append(
									'<div class="no-video-container">' +
										'<i class="fa fa-video-camera fa-5 no-video-icon" style="height: 100%;"></i>' +
										'<span class="no-video-text" style="font-size: 16px;">No webcam available</span>' +
									'</div>');
							}
						},
						onremotestream: function(stream) {
							// The publisher stream is sendonly, we don't expect anything here
						},
						oncleanup: function() {
							Janus.log(" ::: Got a cleanup notification: we are unpublished now :::");
							mystream = null;
							$('#videolocal').html('<button id="publish" class="btn btn-primary">Publish</button>');
							$('#publish').click(function() { publishOwnFeed(true); });
							$("#videos").unblock();
						}
					});
			},
			error: function(error) {
				Janus.error(error);
				bootbox.alert(error, function() {
					window.location.replace(window.location.origin)
				});
			},
			destroyed: function() {
				window.location.replace(window.location.origin)
			}
		});
}

function joinMeeting(){
	if (myusername === null ){
		$('#registernow').removeClass('hide').show();
		registerUsername();
	}else{
		$('#registernow').addClass('hide');
		$('#videojoin').removeClass('hide').show();
		sendJoinMessage()
	}
}

function enableHangup(){
	$('#hangup').click(function() {
		$(this).attr('disabled', true);
		janus.destroy();
		window.location.replace(window.location.origin)
	});
}


function sendJoinMessage(){
	var register = { "request": "join", "room": myroom, "ptype": "publisher", "display": myusername };
	sfutest.send({"message": register});
	enableHangup()
}

function checkEnter(field, event) {
	var theCode = event.keyCode ? event.keyCode : event.which ? event.which : event.charCode;
	if(theCode == 13) {
		// registerUsername();
		return false;
	} else {
		return true;
	}
}

function getFromQueryParams(searchKey) {
	var params = (new URL(document.location)).searchParams;
	return params.get(searchKey)
}

function parseQueryParams(){
	meetingId = getFromQueryParams("meetingId")
	userId = getFromQueryParams("userId")
	myusername = getFromQueryParams("userName")
	myroom = parseInt(getFromQueryParams('room'))
	startTime = getFromQueryParams("startTime")
	// history.pushState("changing url after param extraction", "url", window.location.origin)
}

function registerUsername() {
	if($('#username').length === 0) {
		// Create fields to register
		$('#register').click(registerUsername);
		$('#username').focus();
	} else {
		// Try a registration
		$('#username').attr('disabled', true);
		$('#register').attr('disabled', true).unbind('click');
		var username = $('#username').val();
		if(username === "") {
			$('#you')
				.removeClass().addClass('label label-warning')
				.html("Insert your display name (e.g., pippo)");
			$('#username').removeAttr('disabled');
			$('#register').removeAttr('disabled').click(registerUsername);
			return;
		}
		if(/[^a-zA-Z0-9]/.test(username)) {
			$('#you')
				.removeClass().addClass('label label-warning')
				.html('Input is not alphanumeric');
			$('#username').removeAttr('disabled').val("");
			$('#register').removeAttr('disabled').click(registerUsername);
			return;
		}
		myusername = username;
		sendJoinMessage()
	}
}

function publishOwnFeed(useAudio) {
	// Publish our stream
	$('#publish').attr('disabled', true).unbind('click');
	sfutest.createOffer(
		{
			// Add data:true here if you want to publish datachannels as well
			media: { audioRecv: false, videoRecv: false, audioSend: useAudio, videoSend: true },	// Publishers are sendonly
			success: function(jsep) {
				Janus.debug("Got publisher SDP!");
				Janus.debug(jsep);
				var publish = { "request": "configure", "audio": useAudio, "video": true };
				sfutest.send({"message": publish, "jsep": jsep});
			},
			error: function(error) {
				Janus.error("WebRTC error:", error);
				if (useAudio) {
					 publishOwnFeed(false);
				} else {
					bootbox.alert("WebRTC error... " + JSON.stringify(error));
					$('#publish').removeAttr('disabled').click(function() { publishOwnFeed(true); });
				}
			}
		});
}

function toggleMute() {
	var muted = sfutest.isAudioMuted();
	Janus.log((muted ? "Unmuting" : "Muting") + " local stream...");
	if(muted)
		sfutest.unmuteAudio();
	else
		sfutest.muteAudio();
	muted = sfutest.isAudioMuted();
	if (muted) {
		$('#mic').addClass('icon-active').addClass('icon-microphone-slash').removeClass('icon-microphone')
	}else{
		$('#mic').removeClass('icon-active').removeClass('icon-microphone-slash').addClass('icon-microphone')
	}
}

function toggleVideo() {
	var muted = sfutest.isVideoMuted()
	Janus.log((muted ? "Unmuting" : "Muting") + " local stream...");
	if(muted)
		sfutest.unmuteVideo();
	else
		sfutest.muteVideo();
	muted = sfutest.isVideoMuted()
	if (muted) {
		$('#camera').addClass('icon-active')
	}else {
		$('#camera').removeClass('icon-active')
	}
}

function initProgressbar(currentCallTime){
	currentProgressbarValue = progressbarMaxVal*(currentCallTime/maxCallTime)
	$('#progress-bar').progressbar({
		classes: {
			'ui-progressbar-value': 'progress-bar progress-bar-striped progress-bar-animated progress-bar-success'
		},
		max: progressbarMaxVal,
		value: Math.floor(currentProgressbarValue)
	})
}

function nextProgressbarValue(){
	currentProgressbarValue = currentProgressbarValue + (progressbarMaxVal/(maxCallTime*60))*progressbarRefreshInterval
	return currentProgressbarValue
}

function progressTheBar(){
	nextValue = nextProgressbarValue()
	progressPerc = nextValue*100/progressbarMaxVal
	if (progressPerc < maxProgressPerc){
		$('#progress-bar').progressbar("value", nextValue)
		$('#progress-bar .progress-bar').css("width",progressPerc+"%")
	}else{
		$('#progress-bar .progress-bar').css("width",maxProgressPerc+"%")
		// adjustMarkerPosition()
	}
}

function adjustMarkerPosition(){
	perc = (progressbarRefreshInterval*100)/(maxCallTime*60)
	$.each($('.bar-step'), function(index, marker){
		$(marker).css("left", (parseInt($(marker).css("left"))/$(marker).parent().width())*100 - perc+"%")
	})
}
function onFirstParticipantJoin(){
	$('#videolocal_side').removeClass('hide')
	$('#videolocal').empty().attr("id", "videoremote1").html('<span class="el-participants--item-name"></span>')
}

function unpublishOwnFeed() {
	// Unpublish our stream
	$('#unpublish').attr('disabled', true).unbind('click');
	var unpublish = { "request": "unpublish" };
	sfutest.send({"message": unpublish});
}

function newRemoteFeed(id, display) {
	// A new feed has been published, create a new plugin handle and attach to it as a listener
	var remoteFeed = null;
	janus.attach(
		{
			plugin: "janus.plugin.videoroom",
			opaqueId: opaqueId,
			success: function(pluginHandle) {
				remoteFeed = pluginHandle;
				Janus.log("Plugin attached! (" + remoteFeed.getPlugin() + ", id=" + remoteFeed.getId() + ")");
				Janus.log("  -- This is a subscriber");
				// We wait for the plugin to send us an offer
				var listen = { "request": "join", "room": myroom, "ptype": "listener", "feed": id, "private_id": mypvtid };
				remoteFeed.send({"message": listen});
			},
			error: function(error) {
				Janus.error("  -- Error attaching plugin...", error);
				bootbox.alert("Error attaching plugin... " + error);
			},
			onmessage: function(msg, jsep) {
				Janus.debug(" ::: Got a message (listener) :::");
				Janus.debug(JSON.stringify(msg));
				var event = msg["videoroom"];
				Janus.debug("Event: " + event);
				if(event != undefined && event != null) {
					if(event === "attached") {
						// Subscriber created and attached
						for(var i=1;i<6;i++) {
							if(feeds[i] === undefined || feeds[i] === null) {
								feeds[i] = remoteFeed;
								remoteFeed.rfindex = i;
								break;
							}
						}
						remoteFeed.rfid = msg["id"];
						remoteFeed.rfdisplay = msg["display"];
						if(remoteFeed.spinner === undefined || remoteFeed.spinner === null) {
							var target = document.getElementById('videoremote'+remoteFeed.rfindex);
							remoteFeed.spinner = new Spinner({top:100}).spin(target);
						} else {
							remoteFeed.spinner.spin();
						}
						Janus.log("Successfully attached to feed " + remoteFeed.rfid + " (" + remoteFeed.rfdisplay + ") in room " + msg["room"]);
						// $('#videoremote'+remoteFeed.rfindex).removeClass('hide').html(remoteFeed.rfdisplay).show();
						if (remoteFeed.rfindex === 1){
							onFirstParticipantJoin()
						}
						$('#videoremote'+remoteFeed.rfindex+' .el-participants--item-name').html(remoteFeed.rfdisplay).parent().removeClass('hide').show()
					} else if(msg["error"] !== undefined && msg["error"] !== null) {
						bootbox.alert(msg["error"]);
					} else {
						// What has just happened?
					}
				}
				if(jsep !== undefined && jsep !== null) {
					Janus.debug("Handling SDP as well...");
					Janus.debug(jsep);
					// Answer and attach
					remoteFeed.createAnswer(
						{
							jsep: jsep,
							// Add data:true here if you want to subscribe to datachannels as well
							// (obviously only works if the publisher offered them in the first place)
							media: { audioSend: false, videoSend: false },	// We want recvonly audio/video
							success: function(jsep) {
								Janus.debug("Got SDP!");
								Janus.debug(jsep);
								var body = { "request": "start", "room": myroom };
								remoteFeed.send({"message": body, "jsep": jsep});
							},
							error: function(error) {
								Janus.error("WebRTC error:", error);
								bootbox.alert("WebRTC error... " + JSON.stringify(error));
							}
						});
				}
			},
			webrtcState: function(on) {
				Janus.log("Janus says this WebRTC PeerConnection (feed #" + remoteFeed.rfindex + ") is " + (on ? "up" : "down") + " now");
			},
			onlocalstream: function(stream) {
				// The subscriber stream is recvonly, we don't expect anything here
			},
			onremotestream: function(stream) {
				Janus.debug("Remote feed #" + remoteFeed.rfindex);
				if($('#remotevideo'+remoteFeed.rfindex).length === 0) {
					// No remote video yet
					$('#videoremote'+remoteFeed.rfindex).append('<video class="rounded centered" id="waitingvideo' + remoteFeed.rfindex + '" width=320 height=240 />');
					$('#videoremote'+remoteFeed.rfindex).append('<video class="rounded centered relative hide" id="remotevideo' + remoteFeed.rfindex + '" width="100%" height="100%" autoplay/>');
				}
				$('#videoremote'+remoteFeed.rfindex).append(
					'<span class="label label-primary hide" id="curres'+remoteFeed.rfindex+'" style="position: absolute; bottom: 0px; left: 0px; margin: 15px;"></span>' +
					'<span class="label label-info hide" id="curbitrate'+remoteFeed.rfindex+'" style="position: absolute; bottom: 0px; right: 0px; margin: 15px;"></span>');
				// Show the video, hide the spinner and show the resolution when we get a playing event
				$("#remotevideo"+remoteFeed.rfindex).bind("playing", function () {
					if(remoteFeed.spinner !== undefined && remoteFeed.spinner !== null)
						remoteFeed.spinner.stop();
					remoteFeed.spinner = null;
					$('#waitingvideo'+remoteFeed.rfindex).remove();
					$('#remotevideo'+remoteFeed.rfindex).removeClass('hide');
					var width = this.videoWidth;
					var height = this.videoHeight;
					$('#curres'+remoteFeed.rfindex).removeClass('hide').text(width+'x'+height).show();
					if(adapter.browserDetails.browser === "firefox") {
						// Firefox Stable has a bug: width and height are not immediately available after a playing
						setTimeout(function() {
							var width = $("#remotevideo"+remoteFeed.rfindex).get(0).videoWidth;
							var height = $("#remotevideo"+remoteFeed.rfindex).get(0).videoHeight;
							$('#curres'+remoteFeed.rfindex).removeClass('hide').text(width+'x'+height).show();
						}, 2000);
					}
				});
				Janus.attachMediaStream($('#remotevideo'+remoteFeed.rfindex).get(0), stream);
				var videoTracks = stream.getVideoTracks();
				if(videoTracks === null || videoTracks === undefined || videoTracks.length === 0 || videoTracks[0].muted) {
					// No remote video
					$('#remotevideo'+remoteFeed.rfindex).hide();
					$('#videoremote'+remoteFeed.rfindex).append(
						'<div class="no-video-container">' +
							'<i class="fa fa-video-camera fa-5 no-video-icon" style="height: 100%;"></i>' +
							'<span class="no-video-text" style="font-size: 16px;">No remote video available</span>' +
						'</div>');
				}
				if(adapter.browserDetails.browser === "chrome" || adapter.browserDetails.browser === "firefox") {
					$('#curbitrate'+remoteFeed.rfindex).removeClass('hide').show();
					bitrateTimer[remoteFeed.rfindex] = setInterval(function() {
						// Display updated bitrate, if supported
						var bitrate = remoteFeed.getBitrate();
						$('#curbitrate'+remoteFeed.rfindex).text(bitrate);
					}, 1000);
				}
			},
			oncleanup: function() {
				Janus.log(" ::: Got a cleanup notification (remote feed " + id + ") :::");
				if(remoteFeed.spinner !== undefined && remoteFeed.spinner !== null)
					remoteFeed.spinner.stop();
				remoteFeed.spinner = null;
				$('#waitingvideo'+remoteFeed.rfindex).remove();
				$('#curbitrate'+remoteFeed.rfindex).remove();
				$('#curres'+remoteFeed.rfindex).remove();
				if(bitrateTimer[remoteFeed.rfindex] !== null && bitrateTimer[remoteFeed.rfindex] !== null) 
					clearInterval(bitrateTimer[remoteFeed.rfindex]);
				bitrateTimer[remoteFeed.rfindex] = null;
			}
		});
}
