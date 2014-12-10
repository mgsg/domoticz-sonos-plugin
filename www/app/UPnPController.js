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

		  $.ajax({
			 url: "json.htm?type=devices&filter=upnp&used=true&order=Name&lastupdate="+$.LastUpdateTime,
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
						var bigtext="";
						if (item.SubType == "Sonos") {
							status=item.Data;
							bigtext=item.Data;
						} else if (item.SubType == "UPnP") {
							status=item.Data;
							bigtext=item.Data;
						}
						
						if (typeof item.Usage != 'undefined') {
							bigtext=item.Usage;
						}
						
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
						}
						if ($(id + " #lastupdate").html()!=item.LastUpdate) {
							$(id + " #lastupdate").html(item.LastUpdate);
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

		  var tophtm=
				'\t<table class="bannav" id="bannav" border="0" cellpadding="0" cellspacing="0" width="100%">\n' +
				'\t<tr>\n' +
				'\t  <td align="left"><div id="timesun" /></td>\n';
		  tophtm+=
				'\t</tr>\n' +
				'\t</table>\n';
		  
		  var i=0;
		  $.ajax({
			 url: "json.htm?type=devices&filter=upnp&used=true&order=Name", 
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
				  
				  var xhtm=
						'\t<div class="span4" id="' + item.idx + '">\n' +
						'\t  <section>\n' +
						'\t    <table id="itemtable" border="0" cellpadding="0" cellspacing="0">\n' +
						'\t    <tr>\n';
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
						xhtm+='\t      <td id="name" style="background-color: ' + nbackcolor + ';">' + item.Name + '</td>\n';
						xhtm+='\t      <td id="bigtext">';
						if (item.SubType == "Sonos") {
						  xhtm+=item.Data;
						} else if (item.SubType == "UPnP") {
						  xhtm+=item.Data;
						} 
						xhtm+='</td>\n';
				  xhtm+='\t      <td id="img"><img src="images/';
					var status="";
					if (item.SubType == "UPnP") {
					  xhtm+='Fan48_On.png" height="48" width="48"></td>\n';
					  status=item.Data;
					}
					else if (item.SubType == "Sonos") {
					  xhtm+='text48.png" height="48" width="48"></td>\n';
					  status=item.Data;
					}
					xhtm+=      
						'\t      <td id="status">' + status + '</td>\n' +
						'\t      <td id="lastupdate">' + item.LastUpdate + '</td>\n' +
						'\t      <td id="type">' + item.Type + ', ' + item.SubType + '</td>\n' +
						'\t      <td>';
				  if (item.Favorite == 0) {
					xhtm+=      
						  '<img src="images/nofavorite.png" title="' + $.i18n('Add to Dashboard') +'" onclick="MakeFavorite(' + item.idx + ',1);" class="lcursor">&nbsp;&nbsp;&nbsp;&nbsp;';
				  }
				  else {
					xhtm+=      
						  '<img src="images/favorite.png" title="' + $.i18n('Remove from Dashboard') +'" onclick="MakeFavorite(' + item.idx + ',0);" class="lcursor">&nbsp;&nbsp;&nbsp;&nbsp;';
				  }

				  if (item.Type == "Sonos") {
					xhtm+='<a class="btnsmall" onclick="ShowFanLog(\'#upnpcontent\',\'ShowUPnPs\',' + item.idx + ',\'' + item.Name + '\');" data-i18n="Log">Log</a> ';
					if (permissions.hasPermission("Admin")) {
						xhtm+='<a class="btnsmall" onclick="EditUPnPDevice(' + item.idx + ',\'' + item.Name + '\');" data-i18n="Edit">Edit</a> ';
					}
				  }
				  else {
					xhtm+='<a class="btnsmall" onclick="ShowFanLog(\'#upnpcontent\',\'ShowUPnPs\',' + item.idx + ',\'' + item.Name + '\');" data-i18n="Log">Log</a> ';				  	
					if (permissions.hasPermission("Admin")) {
						xhtm+='<a class="btnsmall" onclick="EditUPnPDevice(' + item.idx + ',\'' + item.Name + '\');" data-i18n="Edit">Edit</a> ';
					}
				  }
				  if (item.ShowNotifications == true) {
					  if (permissions.hasPermission("Admin")) {
						  if (item.Notifications == "true")
							xhtm+='<a class="btnsmall-sel" onclick="ShowNotifications(' + item.idx + ',\'' + item.Name + '\', \'#upnpcontent\', \'ShowUPnPs\');" data-i18n="Notifications">Notifications</a>';
						  else
							xhtm+='<a class="btnsmall" onclick="ShowNotifications(' + item.idx + ',\'' + item.Name + '\', \'#upnpcontent\', \'ShowUPnPs\');" data-i18n="Notifications">Notifications</a>';
					  }
				  }
				  xhtm+=      
						'</td>\n' +
						'\t    </tr>\n' +
						'\t    </table>\n' +
						'\t  </section>\n' +
						'\t</div>\n';
				  htmlcontent+=xhtm;
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
			$scope.mytimer=$interval(function() {
				RefreshUPnPs();
			}, 10000);
		  return false;
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