function getParamsFromString( loc ) {
	var params = {};
	if (loc.length>0 ) {
		var parts = loc.slice(1).split('&');
		if( parts.length>0 ) for( i=0 ; i<parts.length ; i++ ) {
			var pair = parts[i].split('=');
			pair[0] = decodeURIComponent(pair[0]);
			pair[1] = decodeURIComponent(pair[1]).replace(/%3D/g,'=').replace(/%26/g,'&') ;
			params[pair[0]] = (pair[1] !== 'undefined') ? pair[1] : true ;
			pair = null ;
		}
		parts = null ;
	}
	return params;
}
function getParams() {
	var params = {};
	if (location.search) {
		return getParamsFromString(location.search);
	}
	return params;
}
function loadPage(page,fctok,fctko) {
	var xhr = null ;
	try { xhr = new XMLHttpRequest() ;
	} catch(e) {
		try { xhr = new ActiveXObject("Msxml2.XMLHTTP") ;
		} catch (e2) {
			try { xhr = new ActiveXObject("Microsoft.XMLHTTP") ;
			} catch (e) { 
				alert( "No XMLHTTPRequest objects support ...") ;
			}
		}
	}
	xhr.onreadystatechange = function() {
		if(xhr.readyState == 1) {
		} else if(xhr.readyState == 4) {
			if( xhr.status == 200 ) {
				fctok( xhr.responseText ) ;
			} else {
				fctko() ;
			}
		}
	}
	xhr.open( "GET", page, true ) ;
	xhr.withCredentials = true ;
	xhr.send( null ) ;
}

function savePage(url,page,data,fctok,fctko) {
	var xhr = null ;
	try { xhr = new XMLHttpRequest() ;
	} catch(e) {
		try { xhr = new ActiveXObject("Msxml2.XMLHTTP") ;
		} catch (e2) {
			try { xhr = new ActiveXObject("Microsoft.XMLHTTP") ;
			} catch (e) { 
				alert( "No XMLHTTPRequest objects support ...") ;
			}
		}
	}
	xhr.onreadystatechange = function() {
		if(xhr.readyState == 1) {
		} else if(xhr.readyState == 4) {
			if( xhr.status == 200 ) {
				fctok() ;
			} else {
				fctko() ;
			}
		}
	}
	xhr.open( "POST", url, true ) ;
	xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");
	xhr.withCredentials = true ;
	var Params = "page="+page+"&data="+encodeURIComponent(data) ;
	xhr.send( Params ) ;
}
