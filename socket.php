<?php
class temporary_message {
	public $msg;
	public $id;
	public $group;
	public $raw;
};
class search_message {
	public $msg;
	public $id;
};

function temporary() {
	$message = new temporary_message;
	$message->msg = 'temporary';
	$message->id = 1001;
	$message->group = 1;
	$message->raw = 1;
	$json = json_encode($message);

	return $json;
}

function search_message()
{
	$message = new search_message;
	$message->msg = 'update_net';
	$message->id = 1001;
	$json = json_encode($message);
	
	return $json;
}

function talk_to_remote($msg) {
	$json = NULL;
	$client = stream_socket_client("tcp://192.168.1.4:7777", $errno, $errstr, 30);
	if (!$client) {
		echo "$errstr ($errno)<br />\n";
	} else {
		fwrite($client, $msg);
		
		stream_set_timeout($client, 2); // wait data timeout
		$json = json_decode(fread($client, 1024));
		fclose($client);
	}
	return $json;
}
// Loads ini file data
function config_read($config_file) {
    return parse_ini_file($config_file, true);
}
// Update a setting in loaded inifile data
function config_set(&$config_data, $section, $key, $value) {
    $config_data[$section][$key] = $value;
}
// Serializes inifile config data back to disk.
function config_write($config_data, $config_file) {
    $new_content = '';
    foreach ($config_data as $section => $section_content) {
        $section_content = array_map(function($value, $key) {
            return "$key=$value";
        }, array_values($section_content), array_keys($section_content));
        $section_content = implode("\n", $section_content);
        $new_content .= "[$section]\n$section_content\n";
    }
    file_put_contents($config_file, $new_content);
}
	echo "start php\n";
	$result = NULL;//talk_to_remote(search_message());
	if($result != NULL) {
		$msg = $result->{'msg'};
		if(strcmp($msg, "update_net") == 0) {
			echo "get net\n";
		}
	}
class c_wlss {
	public $message;
	public $buildtime;
	public $sensors;
};
class c_sensor {
	public $id;
	public $group;
	public $aquire_cycle;
	public $triger;
	public $raw;
	public $active_slot;
	public $arguments;
};
class c_active {
	public $start;
	public $end;
};
class c_argument {
	public $channel;
	public $freq;
	public $nums;
	public $alarm;
	public $confirm;
};
class c_confirm {
	public $times;
	public $raw;
	public $shadow;
};
class c_alrm {
	public $type;
	public $src;
	public $limit;
};
class c_limit {
	public $low;
	public $high;
};
	$wlss = new c_wlss;
	$wlss->message = 'parameter';
	$wlss->buildtime = date("Y-m-d");
	
	$wlss->sensors = array();
	$sensor = new c_sensor;
	$sensor->id = 10033;
	$sensor->group = 0;
	$sensor->aquire_cycle = 5;
	$sensor->triger = 0;
	$sensor->raw = 1;
	
	$sensor->active_slot = array();
	$active = new c_active;
	$active->start = 0;
	$active->end = 1440;
	$sensor->active_slot[0] = $active;
	
	$sensor->arguments = array();
	$argument = new c_argument;
	$argument->channel = 1;
	$argument->freq = 2000;
	$argument->nums = 2018;
	$sensor->arguments[0] = $argument;
	
	$wlss->sensors[0] = $sensor;
	echo json_encode($wlss)."\n";
	
	$key = 780;
	$channel = array(779=>0, 780=>1, 781=>2, 782=>3, 783=>4, 784=>5, 785=>6, 786=>7, 787=>8);
	foreach($channel as $x=>$x_value) {
		if($key == $x)
			echo "Key=" . $x . ", Value=" . $x_value."\n";
	}
	//echo "php ini test\n";
	//$config_info = config_read("/tmp/wlss.net");
	//config_set($config_info, "LOCAL", "dhcp", "enable");
	//config_write($config_info, "/tmp/wlss.net");
	
	//$config_info = parse_ini_file("/tmp/wlss.net", true);
	//var_dump($config_info);
	//echo $config_info['LOCAL']['dhcp']."\n";

?>
