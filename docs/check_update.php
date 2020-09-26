<?php

if( !isset($_REQUEST["version"]) ) header("Location: http://www.9bis.net/kitty/" ) ;
else {

$bin_version = $_REQUEST["version"] ;
if( !preg_match( "/^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$/", $bin_version ) ) { $bin_version = "0.0.0.0" ; }

if( $fd = fopen( "version.txt", "r") ) {
	$last_version = fgets( $fd, 1024 ) ;
	fclose( $fd ) ;
	}
else { $last_version="0.0.0.0" ; }
$last_version = preg_replace( "/[^0-9\.]/", "", $last_version ) ;

$bin_array=explode( ".", $bin_version ) ;
$last_array=explode( ".", $last_version ) ;

$test = 0 ;

if( $bin_array[0]<$last_array[0] ) $test=1 ;
else if( $bin_array[1]<$last_array[1] ) $test=1 ;
else if( $bin_array[2]<$last_array[2] ) $test=1 ;
else if( $bin_array[3]<$last_array[3] ) $test=1 ;


echo "<html><head>" ;
echo '<script type="text/javascript">
var sec=11 ;
function starttimer() {
	sec = sec -1 ;
	if( sec>=0 ) {
		document.getElementById("t").innerHTML = sec ;
		setTimeout( function(){starttimer()}, 1000 );
	}
	else {
		window.location = "http://www.9bis.net/kitty/" ;
	}
}
</script>' ;
echo "<title>KiTTY version checker</title></head>\n<body onLoad=\"starttimer();\">\n" ;
echo "<center>\n" ;
echo '<script type="text/javascript"><!--
google_ad_client = "ca-pub-8272224832618193";
/* KiTTY */
google_ad_slot = "9739478064";
google_ad_width = 728;
google_ad_height = 90;
//-->
</script>
<script type="text/javascript"
src="http://pagead2.googlesyndication.com/pagead/show_ads.js">
</script>' ;
echo "<br>&nbsp;<br>&nbsp;<br>&nbsp;<br>&nbsp;<br>&nbsp;<br>&nbsp;<br>" ;

if( $test ) {
	echo "<h1>\n" ;
	echo "Your version is ".$bin_version."<br>\n" ;
	echo "The last version is ".$last_version."<br>\n" ;
	echo " <br>\n" ;
	echo "<a href=\"http://www.9bis.net/kitty/\">Upgrade</a>\n" ;
	echo "</h1>\n" ;
}
else {
	echo "<h1>Your version is up to date</h1>\n" ;
}

echo " <br>You will be redirected in <div id=\"t\">10</div> seconds." ;

echo "</center>\n" ;
echo "</body>\n</html>\n" ;

	}
	
?>
