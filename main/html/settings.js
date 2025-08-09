//时间
var ntp = getE("ntp");
var h = getE("hour12");
var ssid = getE("ssid");
var pwd = getE("pwd");
var scan_ssid = getE("scan_ssid");
var scan_pwd = getE("scan_pwd");
var time_interval = getE("time_interval");
var day_fmt = getE("day-format");
var colon = getE("colon");
var dst = getE("dst");
//股票
document.title = "CyberClock";
var theme = getE("theme");

function getData(data) {
	getResponse(data, function(responseText) {
	try {
        res = JSON.parse(responseText);
    } catch(e) {
		return;
    }
	console.log(res);

	//turn off
	var t1 = getE("time1");
	var t2 = getE("time2");
	var time_brt = getE("time_brt");
	var time_brt_en = getE("time_brt_en");
	if(res.a) {ssid.value = res.a; scan_ssid.value = res.a;}
	if(res.p) {pwd.value = res.p; scan_pwd.value = res.p;}
	if(res.t1) t1.value = res.t1;
	if(res.t2) t2.value = res.t2;
	if(res.en) time_brt_en.checked = true;
	
	//24h 12h
	if(res.h == "1") h.checked = true;
	//timezone
	if(res.tz) tz.value = res.tz;
	if(res.mtz) mtz.value = res.mtz;
	
	});
}

function send_http(url){
	getResponse(url, function(responseText) {
		if (responseText == "OK") {
			showPopup("Saved successfully!", 1500, "#02a601");
		}
		else{
			showPopup("Saved failed !", 1500, "#02a601");
		} 
	});
}
function set_c(){
	var copyright = getE("copyright");
	if(copyright != null)copyright.innerHTML = '<br />Copyright (c) 2023 GeekMagic® All rights reserved, Support mail:<a href=\"\" target=\"_blank\"> ifengchao1314@gmail.com</a>';
}
set_c();
function getE(name){
	return document.getElementById(name);
}
function getNav(){
	//return;
	var navLinks = [
		{ href: "network.html", text: "Network" },
		//{ href: "weather.html", text: "Weather" },
		//{ href: "time.html", text: "Time" },
		//{ href: "image.html", text: "Pictures" },
		//{ href: "stock.html", text: "Stocks" },
		//{ href: "daytimer.html", text: "Day Countdown" },
		//{ href: "bili.html", text: "B站粉" },
		//{ href: "monitor.html", text: "电脑性能监视器" },
		{ href: "index.html", text: "System" }
	];

	var dynamicNav = getE("nav");
	dynamicNav.innerHTML = "";
	for (var i = 0; i < navLinks.length; i++) {
		var link = document.createElement("a");
		link.className = "center";
		link.href = navLinks[i].href;
		link.textContent = navLinks[i].text;
		dynamicNav.appendChild(link);
	}
}
function escapeHTML(str) {
	if(str==undefined) return;
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/\"/g, '&quot;')
      .replace(/\'/g, '&#39;')
      .replace(/\//g, '&#x2F;')
}
function showPopup(message, closeAfter,bg) {
	const popup = document.getElementById('popup');
	popup.textContent = message;
	popup.style.opacity = '1';
	popup.style.backgroundColor = bg;

	setTimeout(() => {
		popup.style.opacity = '0';
	}, closeAfter); // 2秒后自动消失
}

function getResponse(adr, callback, timeoutCallback, timeout, method){
	if(timeoutCallback === undefined) {
		timeoutCallback = function(){
			showPopup("Request failed, please try again... ", 2000, "#dc0d04");
		};
	}
	if(timeout === undefined) timeout = 20000; 
	if(method === undefined) method = "GET";
	var xmlhttp = new XMLHttpRequest();
	xmlhttp.onreadystatechange = function() {
		if(xmlhttp.readyState == 4){
			if(xmlhttp.status == 200){
				callback(xmlhttp.responseText);
			}
			else if(xmlhttp.status == 404){
			}
			else timeoutCallback();
		}
	};
	xmlhttp.open(method, adr, true);
	xmlhttp.send();
	xmlhttp.timeout = timeout;
	xmlhttp.ontimeout = timeoutCallback;
}