# Mise en place de l'interface 

Afin de communiquer les données du robot à notre base InfluxDB, différents mécanismes ont été mis en place. Pour pouvoir accéder à l'environnement de développement, il faut lancer la machine virtuelle (fichier ).  
```
Identifiants de connexion de session Ubuntu :
Utilisateur : iona
Mdp : cfai2025
```
Une fois l'environnement Ubuntu dévérouillé, il faut lancer le serveur Influxdb. Cette configuration se fait une seule fois à la création du projet, puis le serveur est lancé au démarrage de la machine virtuelle :  
```
influxdb3 serve --node-id host0 --object-store file --data-dir ~./influxdb3
```
Si le serveur a déjà été configuré, il suffit de fvérifier son état avec :   
```
systemctl status influxdb3-core
```

Ensuite, il suffit d'ouvrir un terminal afin de lancer les conteneurs Docker de grafana et influxdb3-explorer, qui permet d'accéder à l'interface administrateur d'InfluxDB :  
```
sudo docker run --detach --name influxdb3-explorer --publish 8888:80 --publish 8889:8888 influxdata/influxdb3-ui --mode=admin
sudo docker run --name grafana -p 3000:3000 grafana/grafana-oss
```
Lorsque Docker est lancé, il est possible d'accéder à Influxdb sur **http://localhost:8888** et à Grafana sur **http://localhost:3000**. Pour accéder à Grafana, il faut utiliser les identifiants suivants :  
```
Identifiant : admin
Mdp : admin
```
Pour configurer Influxdb, des tokens ont également été générés, et sont disponible sur le bureau de la machine virtuelle. 


