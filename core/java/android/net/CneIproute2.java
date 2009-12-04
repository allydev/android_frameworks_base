/**
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * * Neither the name of Code Aurora nor
 *     the names of its contributors may be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** TODO - Insert {@hide} into all Javadoc statements  */

/** TODO - This class SHOULD NOT be public when it is released. It is
 *  temporarily public to allow the CneIproute2Test APK to run correctly
 */

package android.net;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.HashMap;
import java.util.Map;

import android.util.Log;

/**
 * File: CneIproute2.java Description: This program is an interface to make the
 * necessary calls to iproute2 in order to set up and take down routing tables.
 * These calls are made indirectly over the command line by creating a new
 * process outside of the Java VM. For each routing device visible to the
 * kernel, CneIproute2 allows one table. Each table contains one entry, a path
 * to the gateway address of the routing device. A source address or network
 * prefix is also required in order to instantiate a table, so that packets from
 * that ip address are routed through the device.
 * 
 * @(#) CneIproute2.java
 * @hide
 */
public final class CneIproute2 {
    // Error messages
    static final String ADDING_DUPLICATE_TABLE = "Adding a table that already exists.";

    static final String CHANGE_DEFAULT_NO_ARGS = "A null argument was passed while changing the default table.";

    static final String CHANGE_DEFAULT_SAME_TABLE = "The new default table is the same as the old.";

    static final String CHANGE_DEFAULT_UNDEFINED_TABLE = "Cannot make a nonexistant table the default.";

    static final String CMD_LINE_CALL_FAILED = "Command line call to iproute2 failed.";

    static final String CMD_LINE_NO_ARGS = "No actual command passed to build a command line.";

    static final String DELETING_NO_TABLES_EXIST = "Deleting a table when no table exists.";

    static final String DELETING_UNDEFINED_DEFAULT = "Cannot delete a default table when none exists.";

    static final String DELETING_UNDEFINED_TABLE = "Cannot delete a table that has not been created.";

    static final String FLUSH_CACHE_FAILED = "Attempt to flush the routing cache failed.";

    static final String IP_FORMAT_ERROR = "Ip address entered into CneIproute2 has an incorrect format.";

    static final String MODIFY_RULE_NO_ARGS = "A null argument was passed while modifying a rule.";

    static final String MODIFY_TABLE_NO_ARGS = "A null argument was passed while modifying a routing table.";

    static final String MODIFY_RULE_UNDEFINED_TABLE = "Cannot modify a rule for a nonexistant table.";

    static final String SHOW_TABLE_NO_ARGS = "A null argument was passed while displaying a table.";

    static final String TOO_MANY_TABLES = "Cannot add a table - too many tables already defined.";

    static final String UNSUPPORTED_ACTION = "Unsupported command action found while building the command.";

    // Debug messages
    static final String CMD_LINE_CALL_MADE = "Iproute2 has been called with: ";

    static final String CMD_LINE_EXITVALUE = "Command line process exit value: ";

    static final String COMMAND_ACTION = " Action: ";

    static final String DEVICE_NAME = " Device name: ";

    static final String GATEWAY_ADDRESS = " Gateway address: ";

    static final String IPROUTE2_STANDARD_ERROR = "Standard output from iproute2: ";

    static final String IPROUTE2_STANDARD_OUTPUT = "Standard output from iproute2: ";

    static final String PRIORITY_NUMBER = " Priority number: ";

    static final String SOURCE_PREFIX = " Source Prefix: ";

    static final String TABLE_COUNT = " Total number of tables: ";

    static final String TABLE_NUMBER = " Table number: ";

    static final String LOG_TAG = "CneIproute2";

    // Commands to begin the command line string
    static final String ROUTING_CMD = "ip route";

    static final String RULE_CMD = "ip rule";

    // Keywords used to refine calls to iproute2
    static final String CMD_LINE_DEVICE_NAME = "dev";

    static final String CMD_LINE_GATEWAY_ADDRESS = "via";

    static final String CMD_LINE_PRIORITY_NUMBER = "priority";

    static final String CMD_LINE_SOURCE_PREFIX = "from";

    static final String CMD_LINE_TABLE_NUMBER = "table";

    // Keywords that refer to specific routes or tables
    static final String ALL_TABLES = "all";

    static final String CACHED_ENTRIES = "cache";

    static final String DEFAULT_ADDRESS = "default";

    // Table #253 is the 'defined' default routing table, which should not
    // be overwritten
    static final int MAX_TABLE_NUMBER = 252;

    // Priority number 32766 diverts packets to the main table (Table #254)
    static final int MAX_PRIORITY_NUMBER = 32765;

    private static int activeTableCount = 0;

    private static int nextTableNumber = 1;

    // Permanently maps a device name to a table number in order to
    // efficiently reuse table numbers
    private HashMap<String, Integer> tableNumberMap;

    // Maps the name of a device to its corresponding routing characteristics
    private HashMap<String, DeviceInfo> deviceMap;

    // If a packet does not have an associated rule, it will go to the main
    // routing table and be routed to the following device by default
    private DeviceInfo defaultDevice;

    // List of all actions supported from iproute2
    private enum Cmd_line_actions {
        ADD, DELETE, FLUSH, SHOW
    }

    /**
     * Stores information needed to create a routing table and a rule. This
     * allows the calling class to delete that table without needing to keep
     * track of any characteristics of the device other than its name. Assumes
     * that there can only be 1 rule associated with any defined table.
     */
    private class DeviceInfo {
        // Variables relating to the routing table
        private int tableNumber;

        private String deviceName;

        private String gatewayAddress;

        // Variables relating to the corresponding rule.
        private String sourcePrefix;

        private int priorityNumber;

        private DeviceInfo(String deviceName, int tableNumber, String gatewayAddress,
                String sourcePrefix, int priorityNumber) {
            this.deviceName = deviceName;
            this.tableNumber = tableNumber;
            this.gatewayAddress = gatewayAddress;
            this.sourcePrefix = sourcePrefix;
            this.priorityNumber = priorityNumber;
        }

        private String getDeviceName() {
            return deviceName;
        }

        private String getGatewayAddress() {
            return gatewayAddress;
        }

        private int getPriorityNumber() {
            return priorityNumber;
        }

        private String getSourcePrefix() {
            return sourcePrefix;
        }

        private int getTableNumber() {
            return tableNumber;
        }
    }

    /**
     * Checks the inputted ip address or prefix to make sure that the address is
     * valid. Assumes that an inputted address is an IPv4 address in the form
     * 'x.x.x.x', where 'x' can be any number between 0 and 255. However, an
     * inputted prefix will have the form 'x/y', 'x.x/y', 'x.x.x/y', or
     * 'x.x.x.x/y', where 'x' can be any number between 0 and 255, and 'y' can
     * be any number between 0 and 32. 'y' represents how many bits of the ip
     * address specified are part of the prefix. For example, '1.2.3.0/24'
     * represents a network where all ip address begin with the first 24 bits of
     * '1.2.3.0', which are '1.2.3'. Note error messages are not handled in this
     * function, so that the specifics of an ip address (such as whether the
     * address is a gateway or source address), which are not available in this
     * function, can be logged when checkIPPrefix() returns.
     * 
     * @param ipAddress String representing some IPv4 address or prefix.
     * @return True if the ip address is valid.
     */
    private boolean checkIPPrefix(String ipPrefix) {
        String[] octets, prefixAndItsLength;

        // This occurs if the input is a ip prefix, not an ip address.
        if (ipPrefix.contains("/")) {
            prefixAndItsLength = ipPrefix.split("/");

            if (prefixAndItsLength.length > 2) {
                return false;
            }

            int prefixLength = Integer.parseInt(prefixAndItsLength[1]);

            if ((prefixLength < 0) || (prefixLength > 32)) {
                return false;
            }

            octets = prefixAndItsLength[0].split("\\.");

            if (octets.length > 4) {
                return false;
            }
        }

        // Otherwise the input is a full ip address
        else {
            octets = ipPrefix.split("\\.");

            if (octets.length != 4) {
                return false;
            }
        }

        // In both cases need to check the range of each octet in the ip address
        for (String currentOctet : octets) {
            int octetValue = Integer.parseInt(currentOctet);
            if ((octetValue < 0) || (octetValue > 255)) {
                return false;
            }
        }

        return true;
    }

    /**
     * Flushes the cache after routing table entries are changed
     */
    private void flushCache() {
        if (!cmdLineCaller(ROUTING_CMD, Cmd_line_actions.FLUSH.toString().toLowerCase(),
                CACHED_ENTRIES)) {
            Log.w(LOG_TAG, FLUSH_CACHE_FAILED);
        }
    }

    /**
     * Changes the default route given the name of the device that will be
     * either the new or old default. The default case occurs if a packet is
     * sent from some source address not associated with a defined table. When
     * this occurs, the main table will route these undefined source addresses
     * to the gateway of the defined default device. This function will add or
     * delete that default route in the main table.
     * 
     * @param deviceName Name of the device that will added or deleted as the
     *            default route (Such as wlan or wwan)
     * @param commandAction The action to be taken to the the default route -
     *            either ADD or DELETE
     * @return True if function is successful. False otherwise.
     */
    private boolean modifyDefaultRoute(String deviceName, Cmd_line_actions commandAction) {
        if (deviceName == null) {
            Log.e(LOG_TAG, CHANGE_DEFAULT_NO_ARGS + DEVICE_NAME + deviceName + "." + COMMAND_ACTION
                    + commandAction);
            return false;
        }

        if (deviceMap == null) {
            Log.e(LOG_TAG, CHANGE_DEFAULT_UNDEFINED_TABLE + DEVICE_NAME + deviceName + "."
                    + COMMAND_ACTION + commandAction);
            return false;
        }

        // If the upcoming command line call fails, revert to last default
        // device
        DeviceInfo lastDefaultDevice = defaultDevice;

        String gatewayAddress;

        /**
         * TODO - Check in emulator if deviceName string needs to be trimmed of
         * whitespace and/or changed to lowercase
         */

        switch (commandAction) {
            case ADD:
                if (!deviceMap.containsKey(deviceName)) {
                    Log.e(LOG_TAG, CHANGE_DEFAULT_UNDEFINED_TABLE + DEVICE_NAME + deviceName + "."
                            + COMMAND_ACTION + commandAction);
                    return false;
                }

                defaultDevice = deviceMap.get(deviceName);
                break;

            case DELETE:
                // The following case should only be entered if the default
                // table is being deleted when no tables exist
                if (defaultDevice == null) {
                    Log.e(LOG_TAG, DELETING_UNDEFINED_DEFAULT + DEVICE_NAME + deviceName + ".");
                    return false;
                }
                break;

            default:
                Log.e(LOG_TAG, UNSUPPORTED_ACTION + COMMAND_ACTION + commandAction);
                return false;
        }

        gatewayAddress = defaultDevice.getGatewayAddress();

        if (!cmdLineCaller(ROUTING_CMD, commandAction.toString().toLowerCase(), DEFAULT_ADDRESS,
                CMD_LINE_GATEWAY_ADDRESS, gatewayAddress, CMD_LINE_DEVICE_NAME, defaultDevice
                        .getDeviceName())) {
            defaultDevice = lastDefaultDevice;
            return false;
        }

        if (commandAction.equals(Cmd_line_actions.DELETE)) {
            // After a deletion, there should be default device defined
            // in the main routing table
            defaultDevice = null;
        }

        return true;
    }

    /**
     * Adds or deletes a routing table given the name of the device associated
     * with that table. This routing table has one route, which will route all
     * packets to some gateway address. This function also will call another
     * function to create or delete a rule that maps some source address'
     * packets to this table.
     * 
     * @param deviceName Name of the device whose table will be changed (Such as
     *            wlan or wwan)
     * @param sourcePrefix The source address or network prefix (Such as
     *            37.214.21/24 or 10.156.45.1) - This parameter is only
     *            necessary if a table is being added.
     * @param gatewayAddress The gateway address of the device - This parameter
     *            is only necessary if a table is being added.
     * @param commandAction The action to be performed on the table - either ADD
     *            or DELETE
     * @return True if function is successful. False otherwise.
     */
    private boolean modifyRoutingTable(String deviceName, String sourcePrefix,
            String gatewayAddress, Cmd_line_actions commandAction) {
        if (deviceName == null) {
            Log.e(LOG_TAG, MODIFY_TABLE_NO_ARGS + DEVICE_NAME + deviceName + "." + SOURCE_PREFIX
                    + sourcePrefix + "." + GATEWAY_ADDRESS + gatewayAddress + "." + COMMAND_ACTION
                    + commandAction);
            return false;
        }

        int tableNumber;
        int priorityNumber;

        DeviceInfo currentDevice;

        /**
         * TODO - Check in emulator if deviceName string needs to be trimmed of
         * whitespace and/or changed to lowercase
         */

        switch (commandAction) {
            case ADD:
                if ((sourcePrefix == null) || (gatewayAddress == null)) {
                    Log.e(LOG_TAG, MODIFY_TABLE_NO_ARGS + DEVICE_NAME + deviceName + "."
                            + SOURCE_PREFIX + sourcePrefix + "." + GATEWAY_ADDRESS + gatewayAddress
                            + "." + COMMAND_ACTION + commandAction);
                    return false;
                }

                if (!checkIPPrefix(sourcePrefix)) {
                    Log.e(LOG_TAG, IP_FORMAT_ERROR + SOURCE_PREFIX + sourcePrefix);
                    return false;
                }

                if (!checkIPPrefix(gatewayAddress)) {
                    Log.e(LOG_TAG, IP_FORMAT_ERROR + GATEWAY_ADDRESS + gatewayAddress);
                    return false;
                }

                if (deviceMap == null) {
                    deviceMap = new HashMap<String, DeviceInfo>();
                    tableNumberMap = new HashMap<String, Integer>();
                }

                // If a call to add a routing table overwrites an existing
                // table, the new source and gateway addresses will overwrite
                // the old ones. However, calls to add a duplicate table, where
                // the source and gateway addresses do not change, are ignored
                // and will not be considered a fatal error.
                if (deviceMap.containsKey(deviceName)) {
                    DeviceInfo existingDevice = deviceMap.get(deviceName);

                    if ((!existingDevice.getGatewayAddress().equals(gatewayAddress))
                            || (!existingDevice.getSourcePrefix().equals(sourcePrefix))) {
                        modifyRoutingTable(deviceName, "", "", Cmd_line_actions.DELETE);

                        if (defaultDevice != null) {
                            modifyDefaultRoute("", Cmd_line_actions.DELETE);
                        }
                    }

                    else {
                        Log.w(LOG_TAG, ADDING_DUPLICATE_TABLE + DEVICE_NAME + deviceName + "."
                                + SOURCE_PREFIX + sourcePrefix + "." + GATEWAY_ADDRESS
                                + gatewayAddress + ".");
                        return true;
                    }
                }

                // Instantiating more than 252 tables simultaneously is an error
                if (MAX_TABLE_NUMBER <= tableNumberMap.size()) {
                    Log.e(LOG_TAG, TOO_MANY_TABLES + DEVICE_NAME + deviceName + TABLE_COUNT
                            + tableNumberMap.size());
                    return false;
                }

                // Reuses table numbers of deleted devices
                if (tableNumberMap.containsKey(deviceName)) {
                    tableNumber = tableNumberMap.get(deviceName);
                }

                else {
                    tableNumber = nextTableNumber++;
                    tableNumberMap.put(deviceName, tableNumber);
                }

                // Always map the same rule to the same table number. This
                // allows the reuse of priority numbers.
                priorityNumber = MAX_PRIORITY_NUMBER - tableNumber;

                currentDevice = new DeviceInfo(deviceName, tableNumber, gatewayAddress,
                        sourcePrefix, priorityNumber);
                break;

            case DELETE:
                if ((deviceMap == null) || (deviceMap.isEmpty())) {
                    Log.e(LOG_TAG, DELETING_NO_TABLES_EXIST);
                    return false;
                }

                if (!deviceMap.containsKey(deviceName)) {
                    Log.e(LOG_TAG, DELETING_UNDEFINED_TABLE + DEVICE_NAME + deviceName);
                    return false;
                }

                currentDevice = deviceMap.get(deviceName);
                gatewayAddress = currentDevice.getGatewayAddress();
                tableNumber = currentDevice.getTableNumber();
                break;

            default:
                Log.e(LOG_TAG, UNSUPPORTED_ACTION);
                return false;
        }

        if (!cmdLineCaller(ROUTING_CMD, commandAction.toString().toLowerCase(), DEFAULT_ADDRESS,
                CMD_LINE_GATEWAY_ADDRESS, gatewayAddress, CMD_LINE_DEVICE_NAME, deviceName,
                CMD_LINE_TABLE_NUMBER, Integer.toString(tableNumber))) {
            // If adding a new routing table fails, undo changes to class
            // variables
            if (commandAction.equals(Cmd_line_actions.ADD)) {
                nextTableNumber--;
            }

            return false;
        }

        switch (commandAction) {
            case ADD:
                deviceMap.put(deviceName, currentDevice);
                activeTableCount++;

                // If there is no default table, the new device should be the
                // default.
                if (defaultDevice == null) {
                    modifyDefaultRoute(deviceName, Cmd_line_actions.ADD);
                }

                break;

            case DELETE:
                deviceMap.remove(deviceName);
                activeTableCount--;

                // If there are no more tables, then there should be no default
                // device.
                if (0 == activeTableCount) {
                    modifyDefaultRoute("", Cmd_line_actions.DELETE);
                }

                // If the default table has been deleted and another device is
                // available, set an arbitrary new device as the new default.
                else if (defaultDevice == currentDevice) {
                    String newDefaultName = (String)deviceMap.keySet().toArray()[0];

                    modifyDefaultRoute("", Cmd_line_actions.DELETE);
                    modifyDefaultRoute(newDefaultName, Cmd_line_actions.ADD);
                }

                break;

            default:
        }

        return modifyRule(currentDevice, commandAction);
    }

    /**
     * Adds or deletes a rule given the actual device object of the table
     * associated with that rule. Every defined routing table requires some rule
     * to map packets from some given source address to that routing table. This
     * function takes an object so that after a routing table has been removed,
     * the source prefix, table number, and priority number associated with that
     * table can still be accessed. This allows a call to be made to iproute2 to
     * delete the corresponding rule.
     * 
     * @param currentDevice Information relating to the device whose associated
     *            rule will be added or deleted
     * @param commandAction The action to be performed to the rule - either ADD
     *            or DELETE
     * @return True if function is successful. False otherwise.
     */
    private boolean modifyRule(DeviceInfo currentDevice, Cmd_line_actions commandAction) {
        if (currentDevice == null) {
            Log.e(LOG_TAG, MODIFY_RULE_NO_ARGS);
            return false;
        }

        String deviceName = currentDevice.getDeviceName();

        if (deviceMap == null) {
            Log.e(LOG_TAG, MODIFY_RULE_UNDEFINED_TABLE + DEVICE_NAME + deviceName + "."
                    + COMMAND_ACTION + commandAction);
            return false;
        }

        // If a rule is being added, its corresponding table should exist in
        // the map of all routing tables.
        if ((commandAction.equals(Cmd_line_actions.ADD))
                && (!deviceMap.containsValue(currentDevice))) {
            Log.e(LOG_TAG, MODIFY_RULE_UNDEFINED_TABLE + DEVICE_NAME + deviceName + COMMAND_ACTION
                    + commandAction);
            return false;
        }

        int tableNumber = currentDevice.getTableNumber();
        int priorityNumber = currentDevice.getPriorityNumber();
        String sourcePrefix = currentDevice.getSourcePrefix();

        if (!cmdLineCaller(RULE_CMD, commandAction.toString().toLowerCase(),
                CMD_LINE_SOURCE_PREFIX, sourcePrefix, CMD_LINE_TABLE_NUMBER, Integer
                        .toString(tableNumber), CMD_LINE_PRIORITY_NUMBER, Integer
                        .toString(priorityNumber))) {
            return false;
        }

        return true;
    }

    /**
     * Displays contents of all routing tables
     * 
     * @return True if function is successful. False otherwise.
     */
    private boolean displayAllRoutingTables() {
        return cmdLineCaller(ROUTING_CMD, Cmd_line_actions.SHOW.toString().toLowerCase(),
                CMD_LINE_TABLE_NUMBER, ALL_TABLES);
    }

    /**
     * Displays contents of the inputted routing table
     * 
     * @param deviceName Name of the device whose routing table will be
     *            displayed
     * @return True if function is successful. False otherwise.
     */
    private boolean displayRoutingTable(String deviceName) {
        if (deviceName == null) {
            Log.e(LOG_TAG, SHOW_TABLE_NO_ARGS + DEVICE_NAME + deviceName);
            return false;
        }

        return cmdLineCaller(ROUTING_CMD, Cmd_line_actions.SHOW.toString().toLowerCase(),
                CMD_LINE_TABLE_NUMBER, deviceName);
    }

    /**
     * Displays all rules currently entered in the system
     * 
     * @return True if function is successful. False otherwise.
     */
    private boolean displayRules() {
        return cmdLineCaller(RULE_CMD, Cmd_line_actions.SHOW.toString().toLowerCase());
    }

    /**
     * Sends a call to iproute2 over the command line. This function takes in a
     * list of an arbitrary number of words, which is parsed together into one
     * final string. This string is sent over the command line. To accomplish
     * this call, a new process must be created outside of the Java VM. Two
     * readers are instantiated to monitor any standard error and standard
     * output messages sent out by iproute2. These messages are then passed to
     * the Android log. An example call is 'cmdLineCaller (ip route, show,
     * tables, all)'. *
     * 
     * @param cmdLineWords A list of all words to be concatenated and passsed
     *            over the command line.
     * @return True if function is successful. False otherwise.
     */
    private boolean cmdLineCaller(String... cmdLineWords) {
        if (cmdLineWords == null) {
            Log.e(LOG_TAG, CMD_LINE_NO_ARGS);
            return false;
        }

        // Build command line string containing each inputted word.
        String cmdLineString = cmdLineWords[0];
        for (int wordIndex = 1; wordIndex < cmdLineWords.length; wordIndex++) {
            cmdLineString += " " + cmdLineWords[wordIndex];
        }

        Log.i(LOG_TAG, CMD_LINE_CALL_MADE + cmdLineString);

        try {
            Runtime cmdLineObject = Runtime.getRuntime();
            Process cmdLineProcess = cmdLineObject.exec(cmdLineString);

            // Read in standard error from command line to print to log
            InputStream cmdLineErrorStream = cmdLineProcess.getErrorStream();
            InputStreamReader cmdLineErrorReader = new InputStreamReader(cmdLineErrorStream);
            BufferedReader errorBufferedReader = new BufferedReader(cmdLineErrorReader);

            // Read in standard input from command line to print to log
            InputStream cmdLineOutputStream = cmdLineProcess.getInputStream();
            InputStreamReader cmdLineOutputReader = new InputStreamReader(cmdLineOutputStream);
            BufferedReader outputBufferedReader = new BufferedReader(cmdLineOutputReader);

            String nextOutputLine = null;
            String nextErrorLine = null;

            // Read through both input and error streams
            while (((nextOutputLine = outputBufferedReader.readLine()) != null)
                    || ((nextErrorLine = errorBufferedReader.readLine()) != null)) {
                if (nextOutputLine != null) {
                    Log.d(LOG_TAG, IPROUTE2_STANDARD_OUTPUT + nextOutputLine);
                } else {
                    Log.e(LOG_TAG, IPROUTE2_STANDARD_ERROR + nextErrorLine);
                }
            }

            // Check whether the command line call executed successfully
            int cmdLineExitValue = cmdLineProcess.waitFor();

            errorBufferedReader.close();
            outputBufferedReader.close();

            Log.d(LOG_TAG, CMD_LINE_EXITVALUE + cmdLineExitValue);

            if (0 != cmdLineExitValue) {
                Log.e(LOG_TAG, CMD_LINE_CALL_FAILED + CMD_LINE_EXITVALUE + cmdLineExitValue);
                return false;
            }

            return true;
        }

        catch (Throwable t) {
            Log.e(LOG_TAG, CMD_LINE_CALL_FAILED);
            return false;
        }
    }

    /**
     * Adds a routing table to the system that contains a single default entry,
     * a route to the gateway address of a device. It also adds a rule to route
     * a given source network prefix or address to the new table.
     * 
     * @param deviceName The name of the device whose table will be added (Such
     *            as wlan or wwan)
     * @param sourcePrefix The source network prefix or address that will be
     *            routed to the device (Such as 37.214.21/24 or 10.156.45.1)
     * @param gatewayAddress The gateway address of the device.
     * @return True if function is successful. False otherwise.
     */
    public boolean addRoutingTable(String deviceName, String sourcePrefix, String gatewayAddress) {
        if (!modifyRoutingTable(deviceName, sourcePrefix, gatewayAddress, Cmd_line_actions.ADD)) {
            return false;
        }

        flushCache();
        return true;
    }

    /**
     * Changes the default device where packets are routed to. If some source
     * address does not match an already defined rule, packets from that source
     * address will be routed through the main table to some default device.
     * This function replaces the default route to direct traffic to an
     * inputted, already defined device. A routing table associated with this
     * device must have been added through addRoutingTable() before it can be
     * the default.
     * 
     * @param deviceName A string representing the new default device's
     *            interface name (Such as wlan or wwan)
     * @return True if function is successful. False otherwise.
     */
    public boolean changeDefaultTable(String deviceName) {
        // No need to perform function if the default device will not change
        if ((defaultDevice != null) && (defaultDevice.getDeviceName().equals(deviceName))) {
            Log.w(LOG_TAG, CHANGE_DEFAULT_SAME_TABLE + DEVICE_NAME + defaultDevice.getDeviceName());
            return true;
        }

        if (!modifyDefaultRoute("", Cmd_line_actions.DELETE)) {
            return false;
        }

        if (!modifyDefaultRoute(deviceName, Cmd_line_actions.ADD)) {
            return false;
        }

        flushCache();
        return true;
    }

    /**
     * Deletes a routing table from the system along with the rule corresponding
     * to that table.
     * 
     * @param deviceName The name of the device whose table will be deleted
     *            (Such as wlan or wwan)
     * @return True if function is successful. False otherwise.
     */
    public boolean deleteRoutingTable(String deviceName) {
        if (!modifyRoutingTable(deviceName, "", "", Cmd_line_actions.DELETE)) {
            return false;
        }

        flushCache();
        return true;
    }

    /**
     * Displays the contents of all routing tables for debugging purposes.
     * 
     * @return True if function is successful. False otherwise.
     */
    public boolean showAllRoutingTables() {
        return displayAllRoutingTables();
    }

    /**
     * Displays the contents of the routing table associated with the inputted
     * device name.
     * 
     * @param deviceName The name of the device to be displayed (Usually wlan or
     *            wwan)
     * @return True if function is successful. False otherwise.
     */
    public boolean showRoutingTable(String deviceName) {
        return displayRoutingTable(deviceName);
    }

    /**
     * Displays the rules associated with all tables for debugging purposes.
     * 
     * @return True if function is successful. False otherwise.
     */
    public boolean showRules() {
        return displayRules();
    }
}
