<?php 
if($_SERVER['REQUEST_METHOD'] == 'POST'){
    $alert = $_POST['alert'];
    $pdo = new PDO("mysql:host=localhost;dbname=alert_db", "root", "");
    $stmt = $pdo->prepare("INSERT INTO mediciones (alert) VALUES (?)");
    $stmt->execute([$alert]);
    echo $alert;
}
if ($_SERVER['REQUEST_METHOD']== 'GET'){
    $pdo= new PDO("mysql:host=localhost;dbname=alert_db", "root", "");

    $stmt = $pdo->prepare("SELECT alert FROM mediciones ORDER BY id DESC LIMIT 1");
    $stmt->execute();
    $alert_status = $stmt->fetch();

    $stmt = $pdo->prepare("SELECT fecha FROM mediciones ORDER BY id DESC LIMIT 1");
    $stmt->execute();
    $alert_date = $stmt->fetch();
    echo $alert_status['alert'];
    echo $alert_date['fecha'];
}

?>  
