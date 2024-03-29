#!/bin/bash

# Decide which config to source based on script argument
if [[ $1 == "computers" ]]; then
    source ~/catkin_ws_swarm2/computers.sh
elif [[ $1 == "robots" ]]; then
    source ~/catkin_ws_swarm2/robots.sh
elif [[ $1 == "all" ]]; then
    source ~/catkin_ws_swarm2/computers.sh
    source ~/catkin_ws_swarm2/robots.sh
else
    echo "Invalid argument. Use computers, robots, or all."
    exit 1
fi

COMMAND="gnome-terminal"

for i in "${!HOSTS[@]}"; do
    echo "------------"
    echo "${i}"
    echo "${USERNAMES[i]}"
    
    ssh-keygen -f "$HOME/.ssh/known_hosts" -R "${HOSTS[i]}"
    
    COMMAND+=" --tab --title='${USERNAMES[i]}' -e 'sshpass -p ${PASSWORDS[i]} ssh -t -o StrictHostKeyChecking=no -o HostKeyAlgorithms=ssh-rsa -o ConnectTimeout=2 -l ${USERNAMES[i]} ${HOSTS[i]}'"
done

eval "$COMMAND"
