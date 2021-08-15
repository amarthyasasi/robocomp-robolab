#!/usr/bin/env python3
# -*- coding: utf-8 -*-

#
#    Copyright (C) 2021 by YOUR NAME HERE
#
#    This file is part of RoboComp
#
#    RoboComp is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    RoboComp is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
#

# \mainpage RoboComp::BodyHandJointsDetector
#
# \section intro_sec Introduction
#
# Some information about the component...
#
# \section interface_sec Interface
#
# Descroption of the interface provided...
#
# \section install_sec Installation
#
# \subsection install1_ssec Software depencences
# Software dependences....
#
# \subsection install2_ssec Compile and install
# How to compile/install the component...
#
# \section guide_sec User guide
#
# \subsection config_ssec Configuration file
#
# <p>
# The configuration file...
# </p>
#
# \subsection execution_ssec Execution
#
# Just: "${PATH_TO_BINARY}/BodyHandJointsDetector --Ice.Config=${PATH_TO_CONFIG_FILE}"
#
# \subsection running_ssec Once running
#
#
#

import argparse
# Ctrl+c handling
import signal
import sys, traceback, IceStorm, subprocess, threading, time, os, copy

from rich.console import Console
console = Console()

import interfaces
import os
print(os.getcwd())
from specificworker import *


#SIGNALS handler
def sigint_handler(*args):
    QtCore.QCoreApplication.quit()

if __name__ == '__main__':
    # init core app
    app = QtCore.QCoreApplication(sys.argv)
    parser = argparse.ArgumentParser()
    parser.add_argument('iceconfigfile', nargs='?', type=str, default='etc/config')
    parser.add_argument('--startup-check', action='store_true')

    args = parser.parse_args()

    # create interface manage -> ICE manage
    interface_manager = interfaces.InterfaceManager(args.iceconfigfile)

    # create work for that ICE port
    if interface_manager.status == 0:
        worker = SpecificWorker(interface_manager.get_proxies_map(), args.startup_check)
        worker.setParams(interface_manager.parameters)
    else:
        print("Error getting required connections, check config file")
        sys.exit(-1)

    # link ice with worker
    interface_manager.set_default_hanlder(worker)
    signal.signal(signal.SIGINT, sigint_handler)

    # start no GUI application
    app.exec_()

    # free mem for inference
    interface_manager.destroy()