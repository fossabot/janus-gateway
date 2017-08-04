setInterval(updateMarkerList, 10*1000)

function mark(){
	description = $("#marker-description").val()
	createMarker(description)
	resetMarkerForm()
}

function resetMarkerForm(){
	$("#marker-form").trigger('reset')
}

function createMarker(description, type, topic){
	timestamp = new Date(Date.now() - 30*1000)
	data = {
		"meetingId": meetingId,
		"description": description,
		"createdBy": userId,
		"timestamp": timestamp.toISOString()
	}
	$.ajax({
	  type: "POST",
	  // url: "http://localhost:8080/v1/meetings/"+meetingId+"/markers",
	  url: "https://ether-staging-1553540497.us-east-1.elb.amazonaws.com:8080/v1/meetings/"+meetingId+"/markers",
	  data: JSON.stringify(data),
	  crossDomain: true
	});
	setMarkerOnProgressBar(timestamp)
}

function setMarkerOnProgressBar(timestamp){
	offsetMin = (timestamp - new Date(startTime))/(1000*60)
	leftOffsetPerc = (offsetMin/maxCallTime)*100

	if (currentCallTime >= (maxProgressPerc*maxCallTime)/100){
		leftOffsetPerc = (offsetMin/currentCallTime)*100
	}

	$("#progress-bar").append('<div class="bar-step" style="left: '+leftOffsetPerc+'%"><div class="label-txt icon-crown"> </div></div>')
}

function updateMarkerList(){
	$.ajax({
	  type: "GET",
	  // url: "http://localhost:8080/v1/meetings/"+meetingId+"/markers",
	  url: "https://ether-staging-1553540497.us-east-1.elb.amazonaws.com:8080/v1/meetings/"+meetingId+"/markers",
	  crossDomain: true,
	  success: function(res){
	  	console.log(res)
	  	renderMarkerList(res)
	  }

	});
}

function renderMarkerList(res){
	$('.bar-step').remove()
	res.forEach(function(summary, index){
		setMarkerOnProgressBar(new Date(summary.marker.timestamp))
	})
}
