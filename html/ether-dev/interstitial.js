$(document).ready(function () {

    //Check if the browser is chrome or not
    if (!window.chrome) {
        $("#checkResult").html('Ether currently supports only Chrome.<br>\
        Please <a href="https://www.google.com/chrome/browser/desktop/index.html" > download the latest version of <br> Chrome</a>\
        and try again');
        return;
    } 

    slackAuthCheck()
});

function getFromCookie(cname) {
    var name = cname + "=";
    var ca = document.cookie.split(';');
    for(var i = 0; i < ca.length; i++) {
        var c = ca[i];
        while (c.charAt(0) == ' ') {
            c = c.substring(1);
        }
        if (c.indexOf(name) == 0) {
            return c.substring(name.length, c.length);
        }
    }
    return "";
}

function setCookie(cname, cvalue, exdays) {
    var d = new Date();
    d.setTime(d.getTime() + (exdays*24*60*60*1000));
    var expires = "expires="+ d.toUTCString();
    document.cookie = cname + "=" + cvalue + ";" + expires + ";path=/";
}

function populateEtherAuthFields(etherAuth){
    etherAuthToken = window.atob(etherAuth)
    tokens = etherAuthToken.split(';')
    tokens.forEach(function(token, index) {
        values = token.split('=')
        setCookie(values[0],values[1],7)
    })
}

function slackAuthCheck(){
    var params = (new URL(document.location)).searchParams;
    
    // Parse all the parameters needed by Recorder
    var myroom = parseInt(params.get('room'));
    // Parse parameters needed for normal participant
    var meetingId = params.get("meetingId");
    var workspaceId = params.get("workspaceId");
    var teamId = params.get("teamId");
    var userId = params.get("userId");
    var activeWorkspaceId = params.get("eth_slk_wsid");
    var etherAuth = params.get("auth")
    var slackClientId = window.location.host == "etherbridge.etherlabs.io" ? "154090774151.242075835267" : "154090774151.252655196675"
    var myusername = params.get("userName");
    var myemail = params.get("userEmail");
    
    //Slack check
    var urlParams = "meetingId=" + meetingId + "&room=" + myroom + "&teamId=" + teamId + "&workspaceId=" + workspaceId;
    
    if (etherAuth != null) {
        populateEtherAuthFields(etherAuth)
    }
    
    userId == null ? userId = getFromCookie("eth_slk_uid") : setCookie("eth_slk_uid", userId, 7)
    activeWorkspaceId = getFromCookie("eth_slk_wsid")
    if (activeWorkspaceId == "" || userId == "" || activeWorkspaceId != workspaceId) {
        refUrl = window.location.origin + "/call.html?" + urlParams
        refUrl = window.encodeURIComponent(refUrl)
        window.location = "https://slack.com/oauth/authorize?scope=identity.basic,identity.email,identity.team,identity.avatar&client_id="+slackClientId+"&state="+refUrl+"&team="+teamId
    }

    myusername == null ? myusername = getFromCookie("eth_slk_uname") : setCookie("eth_slk_uname", myusername, 7)
    myemail == null ? myemail = getFromCookie("eth_slk_uemail") : setCookie("eth_slk_uemail", myusername, 7)
    
    window.location.href = "./call.html?" + urlParams;
}
