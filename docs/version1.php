<?php 

$fichier = "version.txt";

$image = imagecreate(148,18);
$colorback = imagecolorallocate($image,255,255,255); // rvb (rouge,vert,bleu) ici noir
$colortext = imagecolorallocate($image,0,0,0); // blanc

header("content-Type: image/jpeg"); 

// lecture du fichier
 if(!($lefichier = @fopen($fichier ,"r"))) {
   ImageString($image,5,1,0,"Version: error",$colortext);
   imagejpeg($image); 
   imagedestroy($image); 
   exit;
  }
 else
  { 
   $str = "Version: ".fgets($lefichier, 20); 
   fclose($lefichier);
  } 

$font = "Ubuntu.ttf"; // it's a Bitstream font check www.gnome.org for more
$font_size = 12;
$angle = 0;
$box = imagettfbbox($font_size, $angle, $font, $str);
$x = (int)(148 - $box[4]) / 2;
$y = (int)(18 - $box[5]) / 2;
imagettftext($image, $font_size, $angle, $x, $y, $colortext, $font, $str);

//ImageString($image,5,1,0,$str,$colortext);

imagejpeg($image); 
imagedestroy($image); 

?>
