setInterval(updateMarkerList, 10*1000)

$('#marker-modal').on('show.bs.modal', function(e) {
  markerType = e.relatedTarget.dataset.type
  $(this).data().type =  e.relatedTarget.dataset.type
  title = e.relatedTarget.dataset.title == null ? 'set a '+markerType+' marker': e.relatedTarget.dataset.title
  $(this).find('.big').html(title.toUpperCase())
});

function mark(){
	description = $("#marker-description").val()
	type = $('#marker-modal').data().type
	createMarker(description, type)
	resetMarkerForm()
}

function resetMarkerForm(){
	$("#marker-form").trigger('reset')
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
	offsetMin = (new Date(marker.timestamp) - new Date(startTime))/(1000*60)

	if (currentCallTime < maxCallTime)
		leftOffsetPerc = (((offsetMin*maxProgressPerc)/100)/maxCallTime)*100
	else
		leftOffsetPerc = (((offsetMin*maxProgressPerc)/100)/currentCallTime)*100
	if (leftOffsetPerc > maxProgressPerc)
		leftOffsetPerc = maxProgressPerc
	$("#progress-bar").append('<div class="bar-step" style="left: '+(leftOffsetPerc-1)+'%"><div class="label-txt '+ markerTypeClassMappinng[marker.type] +'"> </div></div>')
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
	res.forEach(function(summary, index){
		setMarkerOnProgressBar(summary)
	})
}
