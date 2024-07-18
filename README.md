# irrigation
Automated home irrigation based on weather data provided by CIMIS weather stations (https://cimis.water.ca.gov/Default.aspx).
Copyright (C) 2024  Natalie C. Pueyo Svoboda

Code to compute inches of water in soil based on ETo and Precipitation to determine when to run drip irrigation/sprinklers. 

Based on calculations using CIMIS provided ETo and UCANR SLIDE rules (https://ucanr.edu/sites/UrbanHort/Water_Use_of_Turfgrass_and_Landscape_Plant_Materials/SLIDE__Simplified_Irrigation_Demand_Estimation/). 

To run code, make sure to create an IrrigationConfig.h.in file which declares a CIMIS app-key (APP_KEY) and station number (CIMIS_STATION)

Create a build folder in the irrigation project folder to make it easy to change
to run on terminal, use:
  $ cmake --build .

To get line numbers on valgrind, run code below *before* cmake --build
  $ cmake -DCMAKE_BUILD_TYPE=Debug .

Use Valgrind to check for seg faults or memory leaks 
  $ valgrind --leak-check=full --tool=memcheck --track-origins=yes --num-callers=16 --leak-resolution=high ./Irrigation 
