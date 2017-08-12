setInterval(updateMarkerList, 10*1000)

var videoTimeAtSetMarkerClick = 0;

$('#marker-modal').on('show.bs.modal', function(e) {
  markerType = e.relatedTarget.dataset.type
  $(this).data().type =  e.relatedTarget.dataset.type
  title = e.relatedTarget.dataset.title == null ? 'set a '+markerType+' marker': e.relatedTarget.dataset.title
  $(this).find('.big').html(title.toUpperCase())
  videoTimeAtSetMarkerClick = document.getElementById('recordingVideo').currentTime
});

function mark(){
	description = $("#marker-description").val()
	type = $('#marker-modal').data().type
	if (currentMeetingInfo.status === "recording-available"){
		createPostCallMarker(description, type)
	}else{createMarker(description, type)}
	resetMarkerForm()
}

function resetMarkerForm(){
	$("#marker-form").trigger('reset')
}

function createPostCallMarker(description, type){
	markerTime = document.getElementById('recordingVideo').currentTime
	data = {
		"meetingId": meetingId,
		"description": description,
		"createdBy": userId,
		"offset": videoTimeAtSetMarkerClick,
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

function createMarker(description, type){
	timestamp = new Date()
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

function setMarkerOnProgressBar(marker){
	markerTypeClassMappinng = {
		"topic": "icon-crown",
		"action": "icon-star-o",
		"decision": "icon-arrow-swap",
		"priority": "icon-alert",
		"personal": "icon-user-outline"
	}
	if (currentCallTime < maxCallTime)
		leftOffsetPerc = offsetMin*maxProgressPerc/maxCallTime
	else
		leftOffsetPerc = offsetMin*maxProgressPerc/currentCallTime
	if (leftOffsetPerc > maxProgressPerc)
		leftOffsetPerc = maxProgressPerc
	$("#progress-bar").append('<div class="bar-step" style="left: '+(leftOffsetPerc-1)+'%"><div class="label-txt '+ markerTypeClassMappinng[marker.type] +'"> </div></div>')
}

function setPostCallMarkersOnProgressBar(marker){
	markerTypeClassMappinng = {
		"topic": "icon-crown",
		"action": "icon-star-o",
		"decision": "icon-arrow-swap",
		"priority": "icon-alert",
		"personal": "icon-user-outline"
	}
	leftOffsetPerc = (marker.offset/recordingDuration)*100
	$("#progress-bar").append('<div class="bar-step" style="left: '+(leftOffsetPerc-1)+'%"><div onclick=handleClickOnMarker(event) class="label-txt '+ markerTypeClassMappinng[marker.type] +'"> </div></div>')
}

function handleClickOnMarker(){
	event.stopPropagation()
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
	$('.bar-step').remove()
	if (currentMeetingInfo.status === "recording-available"){
		res.forEach(function(summary, index){
			setPostCallMarkersOnProgressBar(summary)
		})
	}
	else{
		res.forEach(function(summary, index){
			setMarkerOnProgressBar(summary)
		})
	}
}
