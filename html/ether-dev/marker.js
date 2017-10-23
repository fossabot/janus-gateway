setInterval(updateMarkerList, markerPollingInterval*1000)

var clickedMarkerId
var numOfMarkers = 0;
var cachedSuggestedMarkers = [];

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
	description = description.replace(/https?:\/\/(www\.)?[-a-zA-Z0-9@:%._\+~#=]{2,256}\.[a-z]{2,6}\b([-a-zA-Z0-9@:%_\+.~#?&//=]*)/g, function(ref) {
		refName = ref
		 if (ref.indexOf("etherlabs.atlassian.net") != -1){
	        refName = ref.split("/").splice(-1)[0]
        }
		return '<a target="_blank" href="'+ref+'"><span class="link">'+refName+'</span></a>'
	})
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

function clearMarkerForm(){
	$("#marker-form").trigger('reset')
}

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
	$("#marker-form").trigger('reset')
}

function createPostCallMarker(description, offset, type){
	data = {
		"recordingId": recordingId,
		"description": description,
		"createdBy": userId,
		"offset": offset,
		"type": type
	}
	$.ajax({
	  type: "POST",
	  url: "https://"+etherHost+"/v1/markers/",
	  data: JSON.stringify(data),
	  crossDomain: true,
	  success: function(res) {
		res.marker.user.name = myusername
		setPostCallMarkersOnProgressBar(res.marker)
	  }
	});
}

function createMarker(description, timestamp, type){
	data = {
		"recordingId": recordingId,
		"description": description,
		"createdBy": userId,
		"timestamp": timestamp.toISOString(),
		"type": type
	}
	$.ajax({
	  type: "POST",
	  url: "https://"+etherHost+"/v1/markers/",
	  data: JSON.stringify(data),
	  crossDomain: true,
	  success: function(res) {
		res.marker.user.name = myusername
		setMarkerOnProgressBar(res.marker)
	  }
	});
}


function setPostCallMarkersOnProgressBar(marker){
	leftOffsetPerc = (marker.offset/recordingDuration)*100
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
	if (shouldRenderMarker(marker, userId)){
		$("#progress-bar").append('<div id="'+marker.id+'"class="bar-step" style="left: '+(leftOffsetPerc-1)+'%"><div data-toggle="modal" data-target="#marker-info-modal" class="label-txt '+ markerTypeClassMappinng[marker.type] +'"> </div></div>')
		$("#"+marker.id).data("info", marker)
	}
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
	if (recordingId != null) {
		$.ajax({
		  type: "GET",
		  url: "https://"+etherHost+"/v1/markers/?recordingId="+recordingId,
		  crossDomain: true,
		  success: function(res){
			renderMarkerList(res.markers)
			searchBarSetMarkers(res.markers)
			},
		  error: function(xhr, res, status){
		  }
		});
	}
}

function shouldRenderMarker(marker, userId){
	if (marker.type != "personal")
		return true
	return marker.createdBy == userId ? true : false
}

function searchBarSetMarkers(res){
	if (currentMeetingInfo.status === "recording-available" && res !=null){
		receivedNumOfMarkers = res.length
		for(var k=1; k < res.length; k++){
			for(var i=k; i > 0 && new Date(res[i].createdAt)<new Date(res[i-1].createdAt); i--){
				var tmpFile = res[i];
				res[i] = res[i-1];
				res[i-1] = tmpFile;
			}
		}

		if(receivedNumOfMarkers !== numOfMarkers){
			$("#searchResults").empty()
			for(marker=0;marker<res.length;marker++){
				if (!res[marker].isSuggested && shouldRenderMarker(res[marker], userId)) {
					description = decorateDescription(res[marker].description)
					$("#searchResults").append('<div class="el-playback-search--result-item">\
					<i class="el-playback-search--result-item-icon '+ markerTypeClassMappinng[res[marker].type]+'">\
					</i><span class="el-playback-search--result-item-title">\
					<h6 class="h6"><span class="divider-dot">'+res[marker].type.toUpperCase()+' &bull; '+res[marker].user.name+' &bull; \
					'+(new Date(res[marker].timestamp)).toString().split(' ', 5)[4]+' &bull; \
					</span><span class="el-playback-search--result-item-watch" onclick=postCallSearchBarWatchClick('+res[marker].offset+')>Watch</span>\
					</h6><span class="el-playback-search--result-item-dic"> \
					<a>'+description+'</a></span>\
					<hr class="markerLine"></div>')
				}
			}
			numOfMarkers = res.length
		}
	}
}

function postCallSearchBarWatchClick(offset){
	$('#recordingVideo')[0].currentTime = offset
	$('#recordingVideo')[0].play()
	$(".icon-playback-play").addClass("hide")
	$(".icon-playback-pause").removeClass('hide')
	$(".el-playback-search--result").addClass("hide")
}

function postCallSearchBarClose(event){
	if($(event.target).closest('.el-playback-search--result').length) {
		if($('.el-playback-search--result').is(":visible")) {
			$('.el-playback-search--result').addClass("hide")
		}
	}
}

function highlightSuggested(marker){
	$(".el-marker--item-icon."+markerTypeClassMappinng[marker.type]).addClass('el-marker-highlight')
	setTimeout(function(){ $(".el-marker--item-icon."+markerTypeClassMappinng[marker.type]).removeClass('el-marker-highlight') }, 5000);

}


function renderMarkerList(res){
	$('.bar-step:not(.el-marker-pending)').remove()
	newSuggestedMarkers = []
	markerRenderMethod = currentMeetingInfo.status === "recording-available" ? setPostCallMarkersOnProgressBar : setMarkerOnProgressBar
	if (res != null) {
		res.forEach(function(summary, index){
			if (summary.isSuggested && cachedSuggestedMarkers.indexOf(summary.id) == -1 && (currentMeetingInfo.status != 'ended' && currentMeetingInfo.status != 'recording-available') && (new Date(summary.createdAt) - currentUserJoinTime) > 0 ){
				cachedSuggestedMarkers.push(summary.id)
				newSuggestedMarkers.push(summary)
			}
			if (!summary.isSuggested) {
				markerRenderMethod(summary)
			}
		})
	}
	newSuggestedMarkers.forEach(function(marker, index) {
		highlightSuggested(marker)
	})
}

function loadMarkerOffset(markerId){
	$.ajax({
	  type: "GET",
	  url: "https://"+etherHost+"/v1/markers/"+markerId,
	  crossDomain: true,
	  success: function(res){
		videoOffset = res.marker.offset
	  },
	  error: function(xhr, res, status){
	  }
	});
}
