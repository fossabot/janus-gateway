var SpeechRecognition = SpeechRecognition || webkitSpeechRecognition
var SpeechGrammarList = SpeechGrammarList || webkitSpeechGrammarList
var SpeechRecognitionEvent = SpeechRecognitionEvent || webkitSpeechRecognitionEvent

var markers = [ 'action' , 'action item', 'agenda' , 'follow up', 'decision', 'decide', 'priority', 'personal',  'Stride', 'Hipchat', 'Jira', 'Confluence', 'Atlassian', 'Slack', 'database','microservice','golang','gokit','Kinesis','Amazon','Microsoft','Video','Audio','Collaboration','Communication','Deep learning','Neural networks','recurrent','reinforcement','Cullen','Sai','Sri','Steve','chat','marker','boomerang','Austin','Bangalore', 'Call part', 'annotation', 'persistence', 'conversation', 'synchronous', 'asynchronous', 'zoom', 'jitsi', 'janus'];
var grammar = '#JSGF V1.0; grammar colors; public <color> = ' + markers.join(' | ') + ' ;'

var startutc = "";
var endutc = "";

var recognition = new SpeechRecognition();
var speechRecognitionList = new SpeechGrammarList();
speechRecognitionList.addFromString(grammar, 1);
recognition.grammars = speechRecognitionList;
recognition.lang = 'en-US';
recognition.maxAlternatives = 1;

function startRecognition() {
  recognition.start();
  console.log('Ready to capture speech');
}

recognition.onresult = function(event) {

  console.log("Speech OnResult")
  var end = new Date();
  endutc = end.toISOString().split('.')[0]+"Z";

  var last = event.results.length - 1;
  var transcriptReceived = event.results[last][0].transcript;
  var confidence = event.results[last][0].confidence

  var data = {}
  data["meetingId"] = meetingId;
  data["startTime"] = startutc;
  data["endTime"] = endutc;
  data["SpokenBy"] = userId;
  data["text"] = transcriptReceived;
  data["confidence"] = confidence;
  data["recordingId"] = recordingId;
  console.log(data)
  $.ajax({
    type: "POST",
    url: "https://"+etherHost+"/v1/transcriptions/segments",
    data: JSON.stringify(data),
    crossDomain: true,
    async: true,
    success: function(res) {
    }
  });
}

recognition.onerror = function(event) {
  console.debug('Error occurred in recognition: ' + event.error);
  setTimeout(function(){recognition.stop()}, 1000)
  setTimeout(function(){recognition.start()}, 1000)
}

recognition.onaudiostart = function() {
  console.debug("Audio start received ")
}

recognition.onaudioend = function() {
  console.debug("Audio end received ")
}

recognition.onsoundstart = function() {
  console.debug("Sound start received ")
}

recognition.onsoundend = function() {
  console.debug("Sound end received ")
}

recognition.onspeechstart = function() {
  console.debug("Speech start received ")
  var start = new Date();
  startutc = start.toISOString().split('.')[0]+"Z";
}

recognition.onspeechend = function() {
  console.debug("Speech end received ")
}

recognition.onstart = function() {
  console.debug("Recognition start received ")
}

recognition.onend = function() {
  console.debug("Recognition end received ")
  recognition.start();
}

recognition.onnomatch = function() {
  console.debug("No match received ")
}
