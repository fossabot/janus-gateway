setInterval(updateMarkerList, markerPollingInterval*1000)

var markerTypeClassMappinng = {
		"topic": "icon-crown",
		"action": "icon-star-o",
		"decision": "icon-arrow-swap",
		"priority": "icon-alert",
		"personal": "icon-user-outline"
	}

$('#marker-modal').on('show.bs.modal', function(e) {
	markerType = e.relatedTarget.dataset.type
	timestamp = new Date(e.timeStamp)
	$(this).data().type =  markerType
	$(this).data().markerTimestamp = timestamp
	$(this).data().videoPlaybackOffset = $('#recordingVideo').currentTime

	title = e.relatedTarget.dataset.title == null ? 'set a '+markerType+' marker': e.relatedTarget.dataset.title
	$(this).find('.big').html(title.toUpperCase())

	setPendingMarkerOnProgressBar(timestamp, e.relatedTarget.dataset.type)
	setTimeout(function (){
		$('#marker-description').focus();
	}, 500)
});

$('#marker-modal').on('hidden.bs.modal', function(e) {
	$('#progress-bar .el-marker-pending').remove()
	$("#marker-form").trigger('reset')
});

function mark(){
	description = $("#marker-description").val()
	type = $('#marker-modal').data().type

	if (currentMeetingInfo.status === "recording-available"){
		offset = $("#marker-modal").data().videoPlaybackOffset
		createPostCallMarker(description, offset, type)
	} else {
		timestamp = $("#marker-modal").data().markerTimestamp
		createMarker(description, timestamp, type)
	}
	resetMarkerForm()
}

function createPostCallMarker(description, offset, type){
	data = {
		"meetingId": meetingId,
		"description": description,
		"createdBy": userId,
		"offset": offset,
		"type": type
	}
	$.ajax({
	  type: "POST",
	  // url: "http://localhost:8080/v1/meetings/"+meetingId+"/markers",
	  url: "https://hive.etherlabs.io:8080/v1/meetings/"+meetingId+"/markers",
	  data: JSON.stringify(data),
	  crossDomain: true,
	  success: function(result) {
		setPostCallMarkersOnProgressBar(result)
	  }
	});
}

function createMarker(description, timestamp, type){
	data = {
		"meetingId": meetingId,
		"description": description,
		"createdBy": userId,
		"timestamp": timestamp.toISOString(),
		"type": type
	}
	$.ajax({
	  type: "POST",
	  // url: "http://localhost:8080/v1/meetings/"+meetingId+"/markers",
	  url: "https://hive.etherlabs.io:8080/v1/meetings/"+meetingId+"/markers",
	  data: JSON.stringify(data),
	  crossDomain: true,
	  success: function(res) {
		setMarkerOnProgressBar(res)
	  }
	});
}


function setPostCallMarkersOnProgressBar(marker){
	leftOffsetPerc = (marker.offset/recordingDuration)*100
	renderMarker(leftOffsetPerc, marker.type)
}

function handleClickOnMarker(){
	event.stopPropagation()
}

function setMarkerOnProgressBar(marker) {
	offsetMin = calcMarkerOffsetMins(marker.timestamp)
	leftOffsetPerc = calcLiveMarkerLeftOffsetPerc(offsetMin)
	renderMarker(leftOffsetPerc, marker.type)
}

function setPendingMarkerOnProgressBar(timestamp, type){
	offsetMin = calcMarkerOffsetMins(timestamp)
	leftOffsetPerc = calcLiveMarkerLeftOffsetPerc(offsetMin)
	renderMarker(leftOffsetPerc, type, true)
}

function renderMarker(leftOffsetPerc, type, pending = false){
	markerClass = pending == true ? "bar-step el-marker-pending" : "bar-step"
	$("#progress-bar").append('<div class="'+markerClass+'" style="left: '+(leftOffsetPerc-1)+'%"><div class="label-txt '+ markerTypeClassMappinng[type] +'"> </div></div>')
}

function calcLiveMarkerLeftOffsetPerc(offsetMin) {
	absTime = currentCallTime < maxCallTime ? maxCallTime : currentCallTime
	leftOffsetPerc = offsetMin*maxProgressPerc/absTime
	return leftOffsetPerc > maxProgressPerc ? maxProgressPerc : leftOffsetPerc
}

function calcMarkerOffsetMins(timestamp){
	return (new Date(timestamp) - new Date(startTime))/(1000*60)
}

function updateMarkerList(){
	$.ajax({
	  type: "GET",
	  // url: "http://localhost:8080/v1/meetings/"+meetingId+"/markers",
	  url: "https://hive.etherlabs.io:8080/v1/meetings/"+meetingId+"/markers",
	  crossDomain: true,
	  success: function(res){
	  	renderMarkerList(res)
	  }
	});
}

function renderMarkerList(res){
	$('.bar-step:not(.el-marker-pending)').remove()
	markerRenderMethod = currentMeetingInfo.status === "recording-available" ? setPostCallMarkersOnProgressBar : setMarkerOnProgressBar
	res.forEach(function(summary, index){
		markerRenderMethod(summary)
	})

}
