# irrigation
Automated home irrigation based on weather data provided by CIMIS weather stations (https://cimis.water.ca.gov/Default.aspx).
Copyright (C) 2024  Natalie C. Pueyo Svoboda

Code to compute inches of water in soil based on ETo and Precipitation to determine when to run drip irrigation/sprinklers. 

Based on calculations using CIMIS provided ETo and UCANR SLIDE rules (https://ucanr.edu/sites/UrbanHort/Water_Use_of_Turfgrass_and_Landscape_Plant_Materials/SLIDE__Simplified_Irrigation_Demand_Estimation/). 

To run code, make sure to create an IrrigationConfig.h.in file which declares a CIMIS app-key (APP_KEY) and station number (CIMIS_STATION)


## cmake reference
Create a build folder in the irrigation project folder to make it easy to change
WITHIN ./project/build/ run:
  $ cmake --build .

If no cache, rerun 
  $ cmake .

To get line numbers on valgrind, run code below *before* cmake --build
  $ cmake -DCMAKE_BUILD_TYPE=Debug .

Use Valgrind to check for seg faults or memory leaks 
  $ valgrind --leak-check=full --tool=memcheck --track-origins=yes --num-callers=16 --leak-resolution=high ./Irrigation 


## Mosquitto for MQTT reference
Starting the process with a non-default config file with -d to run in the background, -c for config file: 
  $ mosquitto -d -c /etc/mosquitto/mosquitto.conf 

Publish a message from server:
Note: if the username has spaces, can use '/' after each word as in other linux commands, e.g. user/ name/ words, or put the words within '', e.g. 'user name words'
  $ mosquitto_pub -h host_name -t /topic_name -m "message" -u pwd_user_name -P pwd

Topic name will determine which relay module will turn on and is named after the section being watered
  e.g. /back_yard

Message sent to turn on relay R for TIME milliseconds would be written as: 
  "R TIME", e.g. "3 4000"


## CRON job reference 
Edit crontab file to create cron jobs
  $ crontab -e

Creating the cron job in crontab file to run once a day at 7am. The job goes to the correct directory, runs the script, and redirects the output to a log file. 
  0 7 * * *  cd /directory/path/to/project && ./Irrigation >> $(date +"%Y%m%d")_log.txt 2>&1

Checking that CRON is running the job at the expected times:
  $ grep CRON /var/log/syslog

