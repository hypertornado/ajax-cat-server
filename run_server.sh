#!/bin/sh
SERVICE='ajax-cat-server'
 
if [ `ps -C $SERVICE | wc -l` -eq 2 ]; then
  echo "Server is running. Everything is OK.";
else
  echo "Server is not running. Starting server.";
  ./$SERVICE
fi
