setInterval(updateMarkerList, markerPollingInterval*1000)

var markerTypeClassMappinng = {
		"topic": "icon-crown",
		"action": "icon-star-o",
		"decision": "icon-arrow-swap",
		"priority": "icon-alert",
		"personal": "icon-user-outline"
	}
$('#marker-info-modal').on('shown.bs.modal', function(e) {
	$(e.relatedTarget).parent().addClass('el-marker-pending').addClass('el-marker--item--clicked')
	offset = $(e.relatedTarget).offset()
	$(this).find('.modal-dialog').css("margin-top", offset.top+60+'px').css('margin-left', offset.left-50+'px')
	markerInfo = $(e.relatedTarget).parent().data().info
	console.log("marker info "+markerInfo)
	$(this).find('.big').html(markerInfo.type.toUpperCase())
	$(this).find('.modal-title .small').empty().html(markerInfo.user.name+" - "+(new Date(markerInfo.timestamp)).toString().split(' ', 5).join(' '))
	$(this).find('.marked-description').html(decorateDescription(markerInfo.description))
});

function decorateDescription(description){
	return description.replace(/@\w+/g, function decorate(ref) { return '<span style="color: dodgerblue;">'+ref+'</span>' })
}

$('#marker-modal').on('show.bs.modal', function(e) {
	markerType = e.relatedTarget.dataset.type
	timestamp = new Date(e.timeStamp)
	$(this).data().type =  markerType
	$(this).data().markerTimestamp = timestamp
	$(this).data().videoPlaybackOffset = $('#recordingVideo')[0].currentTime

	title = e.relatedTarget.dataset.title == null ? 'set a '+markerType+' marker': e.relatedTarget.dataset.title
	$(this).find('.big').html(title.toUpperCase())

	if (currentMeetingInfo.status === "recording-available"){
		setPostCallPendingMarkerOnProgressBar($(this).data().videoPlaybackOffset, e.relatedTarget.dataset.type)
	}else{
		setPendingMarkerOnProgressBar(timestamp, e.relatedTarget.dataset.type)
	}
});

$('#marker-modal').on('shown.bs.modal', function(e) {
	$(e.relatedTarget).addClass('el-marker--item--active')
	$('#marker-description').focus();
});

$('#marker-modal').on('hidden.bs.modal', function(e) {
	$('#progress-bar .el-marker-pending').remove()
	$("#marker-form").trigger('reset')
});

$('#marker-info-modal').on('hidden.bs.modal', function(e) {
	$('#progress-bar .el-marker-pending').removeClass('el-marker--item--clicked').removeClass('el-marker-pending')
});

$('#marker-modal').on('hide.bs.modal', function(e) {
	$('.el-marker--item--active').removeClass('el-marker--item--active')
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
	  success: function(res) {
		res.user.name = myusername
		setPostCallMarkersOnProgressBar(res)
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
		res.user.name = myusername
		setMarkerOnProgressBar(res)
	  }
	});
}


function setPostCallMarkersOnProgressBar(marker){
	leftOffsetPerc = (marker.offset/recordingDuration)*100
	renderMarker(leftOffsetPerc, marker)
}

function handleClickOnMarker(){
	event.stopPropagation()
}

function setMarkerOnProgressBar(marker) {
	offsetMin = calcMarkerOffsetMins(marker.timestamp)
	leftOffsetPerc = calcLiveMarkerLeftOffsetPerc(offsetMin)
	renderMarker(leftOffsetPerc, marker)
}

function setPendingMarkerOnProgressBar(timestamp, type){
	offsetMin = calcMarkerOffsetMins(timestamp)
	leftOffsetPerc = calcLiveMarkerLeftOffsetPerc(offsetMin)
	renderPendingMarker(leftOffsetPerc, type)
}

function setPostCallPendingMarkerOnProgressBar(offset, type){
	leftOffsetPerc = (offset/recordingDuration)*100
	renderPendingMarker(leftOffsetPerc, type)
}

function renderMarker(leftOffsetPerc, marker){
	$("#progress-bar").append('<div id="'+marker.id+'"class="bar-step" style="left: '+(leftOffsetPerc-1)+'%"><div data-toggle="modal" data-target="#marker-info-modal" class="label-txt '+ markerTypeClassMappinng[marker.type] +'"> </div></div>')
	$("#"+marker.id).data("info", marker)
}

function renderPendingMarker(leftOffsetPerc, type){
	$("#progress-bar").append('<div class="bar-step el-marker-pending" style="left: '+(leftOffsetPerc-1)+'%"><div class="label-txt '+ markerTypeClassMappinng[type] +'"> </div></div>')
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
