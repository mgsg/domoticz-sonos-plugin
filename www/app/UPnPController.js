define(['app'], function (app) {
app.controller('UPnPController', [ '$scope', '$rootScope', '$location', '$http', '$interval', 'permissions', function($scope,$rootScope,$location,$http,$interval,permissions) {
MakeFavorite = function(id,isfavorite)
{
if (!permissions.hasPermission("Admin")) {
HideNotify();
ShowNotify($.i18n('You do not have permission to do that!'), 2500, true);
return;
}
if (typeof $scope.mytimer != 'undefined') {
$interval.cancel($scope.mytimer);
$scope.mytimer = undefined;
}
$.ajax({
url: "json.htm?type=command&param=makefavorite&idx=" + id + "&isfavorite=" + isfavorite,
async: false,
dataType: 'json',
success: function(data) {
ShowUPnPs();
}
});
}
SwitchUPnPDevice = function(idx,switchcmd)
{
$.ajax({
url: "json.htm?type=command&param=switchlight&switchcmd=" + switchcmd + "&idx=" + idx + "&level=1",
async: false,
dataType: 'json',
success: function(data) {
ShowUPnPs();
}
});
}
EditUPnPDevice = function(idx,name)
{
if (typeof $scope.mytimer != 'undefined') {
$interval.cancel($scope.mytimer);
$scope.mytimer = undefined;
}
$.devIdx=idx;
$("#dialog-editupnpdevice #devicename").val(name);
$( "#dialog-editupnpdevice" ).dialog( "open" );
}
AddUPnPDevice = function()
{
bootbox.alert($.i18n('Please use the devices tab for this.'));
}
RefreshUPnPs = function()
{
if (typeof $scope.mytimer != 'undefined') {
$interval.cancel($scope.mytimer);
$scope.mytimer = undefined;
}
var id="";
// "/media/upnp.json"
// "json.htm?type=devices&filter=upnp&used=true"
$.ajax({
url: "json.htm?type=devices&filter=upnp&used=true",
async: false,
dataType: 'json',
success: function(data) {
if (typeof data.result != 'undefined') {
if (typeof data.ActTime != 'undefined') {
$.LastUpdateTime=parseInt(data.ActTime);
}
$.each(data.result, function(i,item){
id="#upnpcontent #" + item.idx;
var obj=$(id);
if (typeof obj != 'undefined') {
if ($(id + " #name").html()!=item.Name) {
$(id + " #name").html(item.Name);
}
var img="";
var status="";
var internalstate="";
var bigtext="";
status=TranslateStatus(item.Status);
internalstate=item.InternalState;
bigtext=item.InternalState; // Data;
var nbackcolor="#D4E1EE";
if (item.Protected==true) {
nbackcolor="#A4B1EE";
}
if (item.HaveTimeout==true) {
nbackcolor="#DF2D3A";
}
else {
var BatteryLevel=parseInt(item.BatteryLevel);
if (BatteryLevel!=255) {
if (BatteryLevel<=10) {
nbackcolor="#DDDF2D";
}
}
}
var obackcolor=rgb2hex($(id + " #name").css( "background-color" )).toUpperCase();
if (obackcolor!=nbackcolor) {
$(id + " #name").css( "background-color", nbackcolor );
}
if ($(id + " #status").html()!=status) {
$(id + " #bigtext").html(bigtext);
$(id + " #status").html(status);
$(id + " #internalstate").html(internalstate);
if (item.InternalState=="Playing") {
img+='<img src="images/hold.png" title="' + $.i18n("Turn Off") + '" onclick="SwitchLight(' + item.idx + ',\'Off\',RefreshLights,' + item.Protected +');" class="lcursor" height="48" width="48">';
} else {
img+='<img src="images/program.png" title="' + $.i18n("Turn On") + '" onclick="SwitchLight(' + item.idx + ',\'On\',RefreshLights,' + item.Protected +');" class="lcursor" height="48" width="48">';
}
}
if ($(id + " #lastupdate").html()!=item.LastUpdate) {
$(id + " #lastupdate").html(item.LastUpdate);
}
var dslider=$(id + " #slider");
if (typeof dslider != 'undefined') {
dslider.slider( "value", item.LevelInt+1 );
}
if (img!="")
{
if ($(id + " #img").html()!=img) {
$(id + " #img").html(img);
}
}
}
});
}
}
});
$rootScope.RefreshTimeAndSun();
$scope.mytimer=$interval(function() {
RefreshUPnPs();
}, 10000);
}
ShowUPnPs = function()
{
if (typeof $scope.mytimer != 'undefined') {
$interval.cancel($scope.mytimer);
$scope.mytimer = undefined;
}
$('#modal').show();
var htmlcontent = '';
var bHaveAddedDevider = false;
var bAllowWidgetReorder=true;
var suntext='<div id="timesun" />\n';
var tophtm=
'\t<table class="bannav" id="bannav" border="0" cellpadding="0" cellspacing="0" width="100%">\n' +
'\t<tr><td><p>UPnP</p></td></tr><tr>\n' +
'\t <td align="left"><div id="timesun" /></td>\n';
tophtm+=
'\t</tr>\n' +
'\t</table>\n';
var i=0;
// "media/upnp.json"
// "json.htm?type=devices&filter=upnp&used=true"
$.ajax({
url: "json.htm?type=devices&filter=upnp&used=true",
async: false,
dataType: 'json',
success: function(data) {
if (typeof data.result != 'undefined') {
$.FiveMinuteHistoryDays=data["5MinuteHistoryDays"];
if (typeof data.WindScale != 'undefined') {
$.myglobals.windscale=parseFloat(data.WindScale);
}
if (typeof data.WindSign != 'undefined') {
$.myglobals.windsign=data.WindSign;
}
if (typeof data.TempScale != 'undefined') {
$.myglobals.tempscale=parseFloat(data.TempScale);
}
if (typeof data.TempSign != 'undefined') {
$.myglobals.tempsign=data.TempSign;
}
if (typeof data.ActTime != 'undefined') {
$.LastUpdateTime=parseInt(data.ActTime);
}
bAllowWidgetReorder=data.AllowWidgetOrdering;
$.each(data.result, function(i,item){
if (item.Unit == "0") {
if (i % 3 == 0)
{
//add devider
if (bHaveAddedDevider == true) {
//close previous devider
htmlcontent+='</div>\n';
}
htmlcontent+='<div class="row divider">\n';
bHaveAddedDevider=true;
}
var img_device = item.StrParam2;
var track = item.StrParam1; // atob(item.StrParam1);
var xhtm=	'\t<div class="span4" id="' + item.idx + '">\n' +
'\t <section>\n' +
'\t <table id="itemtable" border="0" cellpadding="0" cellspacing="0">\n' +
'\t <tr>\n';
var nbackcolor="#D4E1EE";
if (item.Protected==true) {
nbackcolor="#A4B1EE";
}
if (item.HaveTimeout==true) {
nbackcolor="#DF2D3A";
}
else {
var BatteryLevel=parseInt(item.BatteryLevel);
if (BatteryLevel!=255) {
if (BatteryLevel<=10) {
nbackcolor="#DDDF2D";
}
}
}
xhtm+='\t <td id="name" style="background-color: ' + nbackcolor + ';">' + item.Name + '</td>\n';
xhtm+='\t <td id="bigtext">';
xhtm+=item.InternalState;
xhtm+='</td>\n';
xhtm+='\t <td id="img">';
if (item.InternalState=="Playing") {
xhtm+='<img src="images/hold.png" title="' + $.i18n("Turn Off") + '" onclick="SwitchLight(' + item.idx + ',\'Off\',RefreshLights,' + item.Protected +');" class="lcursor" height="48" width="48">';
} else {
xhtm+='<img src="images/program.png" title="' + $.i18n("Turn On") + '" onclick="SwitchLight(' + item.idx + ',\'On\',RefreshLights,' + item.Protected +');" class="lcursor" height="48" width="48">';
}
xhtm+='</td>\n';
// We store in StrParam2 the UPnP Image of the device!!!
// We store in StrParam1 the Track info!!!
xhtm+= '\t <td id="status">' + TranslateStatus(item.Status) + '</td>\n' +
'\t <td id="internalstate">' + item.InternalState + '</td>\n' +
'\t <td id="lastupdate">' + item.LastUpdate + '</td>\n';
xhtm+= '\t <td id="img2"><img src="' + img_device + '" title="' + $.i18n(img_device) + '" class="lcursor" height="48" width="48"></td>';
// xhtm+= '\t <td id="img2"><img src="images/Printer48_On.png" class="lcursor" height="48" width="48"></td>';
xhtm+= '\t <td id="type">Type: ' + item.Type + ', ' + item.SubType + '</td>\n' +
'\t <td id="track">Track:' + track + '</td>\n';
// xhtm+= '<td><div style="margin-left:0px;" width="90%" class="dimslider" id="slider" data-idx="' + item.idx + '" data-type="norm" data-maxlevel="' + item.MaxDimLevel + '" data-isprotected="' + item.Protected + '" data-svalue="' + item.LevelInt + '"></div><br></td>';
xhtm+= '\t<td><div style="margin-left:10px;" class="dimslider" id="slider" data-idx="' + item.idx + '" data-type="norm" data-maxlevel="' + item.MaxDimLevel + '" data-isprotected="' + item.Protected + '" data-svalue="' + item.LevelInt + '"></div><br></td>';
// Bottom buttom area
xhtm+= '<td>';
if (item.Favorite == 0) {
xhtm+=
'<img src="images/nofavorite.png" title="' + $.i18n('Add to Dashboard') +'" onclick="MakeFavorite(' + item.idx + ',1);" class="lcursor">&nbsp;&nbsp;&nbsp;&nbsp;';
} else {
xhtm+=
'<img src="images/favorite.png" title="' + $.i18n('Remove from Dashboard') +'" onclick="MakeFavorite(' + item.idx + ',0);" class="lcursor">&nbsp;&nbsp;&nbsp;&nbsp;';
}
if (permissions.hasPermission("Admin")) {
xhtm+='<a class="btnsmall" onclick="EditUPnPDevice(' + item.idx + ',\'' + item.Name + '\');" data-i18n="Edit">Edit</a>&nbsp;&nbsp;';
}
xhtm+= '<img src="images/Speaker48_On.png" title="' + $.i18n("Say") + '" onclick="SwitchUPnPDevice('+item.idx+',\'TTS\');" class="lcursor" height="24" width="24"><img src="images/spacer.gif" height="10" width="15">' +
'<img src="images/lux48.png" title="' + $.i18n("Preset1") + '" onclick="SwitchUPnPDevice('+item.idx+',\'Preset1\');" class="lcursor" height="24" width="24"><img src="images/spacer.gif" height="10" width="10">' +
'<img src="images/lux48.png" title="' + $.i18n("Preset2") + '" onclick="SwitchUPnPDevice('+item.idx+',\'Preset2\');" class="lcursor" height="24" width="24">';
// xhtm+= '<a class="btnsmall" onclick="SwitchUPnPDevice('+item.idx+',\'TTS\');" data-i18n="TTS">TTS</a>&nbsp;&nbsp;' +
// '<a class="btnsmall" onclick="SwitchUPnPDevice('+item.idx+',\'Preset1\');" data-i18n="Preset 1">Preset 1</a>&nbsp;&nbsp;' +
// '<a class="btnsmall" onclick="SwitchUPnPDevice('+item.idx+',\'Preset2\');" data-i18n="Preset 2">Preset 2</a>&nbsp;&nbsp;';
xhtm+= '</td>\t </tr>\n' +
'\t </table>\n' +
'\t </section>\n' +
'\t</div>\n';
htmlcontent+=xhtm;
}
});
}
}
});
if (bHaveAddedDevider == true) {
//close previous devider
htmlcontent+='</div>\n';
}
if (htmlcontent == '')
{
htmlcontent='<h2>' + $.i18n('No UPnP devices found or added in the system...') + '</h2>';
}
$('#modal').hide();
$('#upnpcontent').html(tophtm+htmlcontent);
$('#upnpcontent').i18n();
if (bAllowWidgetReorder==true) {
if (permissions.hasPermission("Admin")) {
if (window.myglobals.ismobileint==false) {
$("#upnpcontent .span4").draggable({
drag: function() {
if (typeof $scope.mytimer != 'undefined') {
$interval.cancel($scope.mytimer);
$scope.mytimer = undefined;
}
$.devIdx=$(this).attr("id");
$(this).css("z-index", 2);
},
revert: true
});
$("#upnpcontent .span4").droppable({
drop: function() {
var myid=$(this).attr("id");
$.devIdx.split(' ');
$.ajax({
url: "json.htm?type=command&param=switchdeviceorder&idx1=" + myid + "&idx2=" + $.devIdx,
async: false,
dataType: 'json',
success: function(data) {
ShowUPnPs();
}
});
}
});
}
}
}
$rootScope.RefreshTimeAndSun();
//Create Dimmer Sliders
$('#upnpcontent .dimslider').slider({
//Config
range: "min",
min: 1,
max: 16,
value: 5,
//Slider Events
create: function(event,ui ) {
$( this ).slider( "option", "max", $( this ).data('maxlevel'));
$( this ).slider( "option", "type", $( this ).data('type'));
$( this ).slider( "option", "isprotected", $( this ).data('isprotected'));
$( this ).slider( "value", $( this ).data('svalue')+1 );
},
slide: function(event, ui) { //When the slider is sliding
clearInterval($.setDimValue);
var maxValue=$( this ).slider( "option", "max");
var dtype=$( this ).slider( "option", "type");
var isProtected=$( this ).slider( "option", "isprotected");
var fPercentage=0;
if (ui.value!=1) {
fPercentage=parseInt((100.0/(maxValue-1))*((ui.value-1)));
}
var idx=$( this ).data('idx');
id="#upnpcontent #" + idx;
var obj=$(id);
if (typeof obj != 'undefined') {
var img="";
var status="";
if (fPercentage==0)
{
img='<img src="images/program.png" title="' + $.i18n("Turn On") + '" onclick="SwitchLight(' + idx + ',\'On\',RefreshLights,' + isProtected +');" class="lcursor" height="48" width="48">';
status="Stopped";
}
else {
img='<img src="images/hold.png" title="' + $.i18n("Turn Off") +'" onclick="SwitchLight(' + idx + ',\'Off\',RefreshLights,' + isProtected +');" class="lcursor" height="48" width="48">';
status="Playing: " + fPercentage + "%";
}
if ($(id + " #img").html()!=img) {
$(id + " #img").html(img);
}
if ($(id + " #status").html()!=status) {
$(id + " #status").html(status);
}
}
$.setDimValue = setInterval(function() { SetDimValue(idx,ui.value); }, 500);
}
});
ResizeDimSliders();
$scope.mytimer=$interval(function() {
RefreshUPnPs();
}, 10000);
return false;
}
ResizeDimSliders = function()
{
var width=$(".span4").width()-90;
$("#upnpcontent .dimslider").width(width);
}
init();
function init()
{
//global var
$.devIdx=0;
$.LastUpdateTime=parseInt(0);
$.myglobals = {
TimerTypesStr : [],
SelectedTimerIdx: 0
};
$('#timerparamstable #combotype > option').each(function() {
$.myglobals.TimerTypesStr.push($(this).text());
});
$( "#dialog-editupnpdevice" ).dialog({
autoOpen: false,
width: 450,
height: 160,
modal: true,
resizable: false,
buttons: {
"Update": function() {
var bValid = true;
bValid = bValid && checkLength( $("#dialog-editupnpdevice #devicename"), 2, 100 );
if ( bValid ) {
$( this ).dialog( "close" );
$.ajax({
url: "json.htm?type=setused&idx=" + $.devIdx + '&name=' + encodeURIComponent($("#dialog-editupnpdevice #devicename").val()) + '&used=true',
async: false,
dataType: 'json',
success: function(data) {
ShowUPnPs();
}
});
}
},
"Remove Device": function() {
$( this ).dialog( "close" );
bootbox.confirm($.i18n("Are you sure to remove this Device?"), function(result) {
if (result==true) {
$.ajax({
url: "json.htm?type=setused&idx=" + $.devIdx + '&name=' + encodeURIComponent($("#dialog-editupnpdevice #devicename").val()) + '&used=false',
async: false,
dataType: 'json',
success: function(data) {
ShowUPnPs();
}
});
}
});
},
"Replace": function() {
$( this ).dialog( "close" );
ReplaceDevice($.devIdx,ShowUPnPs);
},
Cancel: function() {
$( this ).dialog( "close" );
}
},
close: function() {
$( this ).dialog( "close" );
}
});
ShowUPnPs();
$( "#dialog-editupnpdevice" ).keydown(function (event) {
if (event.keyCode == 13) {
$(this).siblings('.ui-dialog-buttonpane').find('button:eq(0)').trigger("click");
return false;
}
});
};
$scope.$on('$destroy', function(){
if (typeof $scope.mytimer != 'undefined') {
$interval.cancel($scope.mytimer);
$scope.mytimer = undefined;
}
});
} ]);
});
