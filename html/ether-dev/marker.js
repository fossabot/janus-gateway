setInterval(updateMarkerList, markerPollingInterval*1000)

var clickedMarkerId
var numOfMarkers = 0;

var markerTypeClassMappinng = {
		"topic": "icon-crown",
		"action": "icon-star-o",
		"decision": "icon-arrow-swap",
		"priority": "icon-alert",
		"fyi": "icon-alert",
		"personal": "icon-user-outline"
	}
$('#marker-info-modal').on('shown.bs.modal', function(e) {
	$(e.relatedTarget).parent().addClass('el-marker-pending').addClass('el-marker--item--clicked')
	offset = $(e.relatedTarget).offset()
	leftOffset = calModalDisplayPostion(offset)
	$(this).find('.modal-dialog').css("margin-top", offset.top+60+'px').css('margin-left', offset.left-leftOffset+'px')
	markerInfo = $(e.relatedTarget).parent().data().info
	clickedMarkerId = markerInfo.id
	console.log("marker info "+markerInfo)
	$(this).find('.big').html(markerInfo.type.toUpperCase())
	$(this).find('.modal-title .small').empty().html(markerInfo.user.name+" - "+(new Date(markerInfo.timestamp)).toString().split(' ', 5).join(' '))
	$(this).find('.marked-description').html(decorateDescription(markerInfo.description))
});

function decorateDescription(description) {
	description = description.replace(/https?:\/\/(www\.)?[-a-zA-Z0-9@:%._\+~#=]{2,256}\.[a-z]{2,6}\b([-a-zA-Z0-9@:%_\+.~#?&//=]*)/g, function(ref) { return '<a target="_blank" href="'+ref+'"><span class="link">'+ref+'</span></a>'})
	return description.replace(/@\w+/g, function decorate(ref) { return '<span style="font-weight: bold;">'+ref+'</span>' }).replace(/\n/g, "<br />")
}

function calModalDisplayPostion(offset) {
	progressBarOffsetRange = parseInt($("#progress-bar").css("width")) +$("#progress-bar").offset().left
	oneThirdOfProgressBar = progressBarOffsetRange/3
	if(offset.left>oneThirdOfProgressBar && offset.left<(progressBarOffsetRange*75/100)){
		return ($(".modal-content.marker-modal-content").parent().width() - 300)
	}if(offset.left>(progressBarOffsetRange - oneThirdOfProgressBar) && offset.left<progressBarOffsetRange){
		return ($(".modal-content.marker-modal-content").parent().width() - 95)
	}
	else{
		return 70
	}
}

function postCallMarkerWatchClick(e){
	$('#recordingVideo')[0].currentTime = $("#"+clickedMarkerId).data().info.offset
	$('#recordingVideo')[0].play()
	$(".icon-playback-play").addClass("hide")
	$(".icon-playback-pause").removeClass('hide')
}

$('#marker-modal').on('show.bs.modal', function(e) {
	markerType = e.relatedTarget.dataset.type
	timestamp = new Date(e.timeStamp)
	$(this).data().type =  markerType
	$(this).data().markerTimestamp = timestamp
	$(this).data().videoPlaybackOffset = $('#recordingVideo')[0].currentTime

	title = e.relatedTarget.dataset.title == null ? 'set a '+markerType+' marker': e.relatedTarget.dataset.title
	$(this).find('.big').html(title.toUpperCase())
	description = e.relatedTarget.dataset.description
	if (description != null)
		$(this).find('.small').empty().html(description)
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

$("#marker-description").on('focus keyup', function() {
		description = $("#marker-description").val()
		if ($.trim(description) == ""){
			$("#markButton").prop("disabled","true")
			$(".btn-primary").css("background-color","rgba(255, 255, 255, 0)")
			$(".el-btn-save").css("color","rgba(58, 58, 58, 1)");
		}else{
			$("#markButton").removeAttr("disabled")
			$(".el-btn-save").css("color","rgba(4,158,193, 1)");
			}
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
	  url: "https://"+etherHost+"/v1/meetings/"+meetingId+"/markers",
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
	  url: "https://"+etherHost+"/v1/meetings/"+meetingId+"/markers",
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
	console.log(leftOffsetPerc)
	if(leftOffsetPerc < 0){
		leftOffsetPerc = 0;
	}
	else if(leftOffsetPerc > 100){
		leftOffsetPerc = 100;
	}
	renderMarker(leftOffsetPerc, marker)
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
	  url: "https://"+etherHost+"/v1/meetings/"+meetingId+"/markers",
	  crossDomain: true,
	  success: function(res){
		renderMarkerList(res)
		searchBarSetMarkers(res)
		},
	  error: function(xhr, res, status){
	  }
	});
}

function searchBarSetMarkers(res){
	receivedNumOfMarkers = res.length
	if(receivedNumOfMarkers !== numOfMarkers){
		$("#searchResults").empty()
		for(marker=0;marker<res.length;marker++){
			description = decorateDescription(res[marker].description)
			$("#searchResults").append('<div class="el-playback-search--result-item">\
			<i class="el-playback-search--result-item-icon '+ markerTypeClassMappinng[res[marker].type]+'">\
			</i><span class="el-playback-search--result-item-title">\
			<h6 class="h6"><span class="divider-dot">'+res[marker].type.toUpperCase()+' &bull; '+res[marker].user.name+' &bull; \
			'+(new Date(res[marker].timestamp)).toString().split(' ', 5)[4]+' &bull; \
			</span><span class="el-playback-search--result-item-watch" onclick=postCallSearchBarWatchClick('+res[marker].offset+')>Watch</span>\
			</h6><small class="el-playback-search--result-item-dic"> \
			<a>'+description+'</a></small></div>')
		}
		numOfMarkers = res.length
	}
}

function postCallSearchBarWatchClick(offset){
	$('#recordingVideo')[0].currentTime = offset
	$('#recordingVideo')[0].play()
	$(".icon-playback-play").addClass("hide")
	$(".icon-playback-pause").removeClass('hide')
	$(".el-playback-search--result").addClass("hide")
}

function renderMarkerList(res){
	$('.bar-step:not(.el-marker-pending)').remove()
	markerRenderMethod = currentMeetingInfo.status === "recording-available" ? setPostCallMarkersOnProgressBar : setMarkerOnProgressBar
	if (res != null) {
		res.forEach(function(summary, index){
			markerRenderMethod(summary)
		})
	}
}
