
Showtime Plugin documentation for developers
============================================

by Andreas Öman, Project Leader

This document is still work-in-progress. Some sections are missing and
some are not complete. If you feel something particular is missing,
please say so.

-- Andreas, andreas@lonelycoder.com or http://twitter.com/andoma

# Overview

Plugins for Showtime are currently written in Javascript. 
The Javascript engine inside Showtime is Spidermonkey 1.8.0-rc1.
Note that Showtime does not implement the same Javascript API as normal
web browsers. The basic Javascript is there but there is no such thing
as a DOM or AJAX, etc. 

This document assumes the reader has good understanding of Javascript.
There are plenty of books and sites on the subject so I don't want to
waste space or time on teaching Javascript.

The Showtime plugins does not interact directly to the user via the
user interface (similar to a web browser) but rather it responds to
browse and search requests and populate the internal data model with
information that is then presented to the user via Showtime's user
interface(s)


# Structure of a plugin

Each plugin resides in a directory of their own. This directory must
contain a file called ''plugin.json'' which contain information about
the plugin.  For more information about this file, please see the
section named plugin.json below.

Apart for the plugin.json file there are no further restrictions of
what the files are named in the plugin directory or if files are
placed in sub-directories, etc

## plugin.json

plugin.json is a JSON (http://www.json.org/) encoded text file
containing information about the plugin

Example of a plugin.json file (from the Headweb plugin):


    {
      "type": "javascript",
      "id": "headweb",
      "file": "headweb.js",
      "showtimeVersion": "3.1",
      "version": "1.0",
      "author": "Andreas Öman",
      "title": "Headweb",
      "icon": "headweb_square.png",
      "synopsis": "Headweb online video",
      "description": "<p>Headweb is a Swedish online video store.<p>For more information, visit <a href=\"http://www.headweb.com\">http://www.headweb.com</a>"
    }



Description of fields and requirement of their presence:

### type (REQUIRED)

Type of plugin, currently only "javascript" is supported.

### id (REQUIRED)

Unique identifier for a plugin. The IDs are assigned by the Showtime
project. Any ID starting with the string "test" is reserved for
development and can be used by plugin developers until a final ID has
been assigned.  The assigned IDs will be ASCII lowercase.  To get an
ID please mail andreas@lonelycoder.com

### file (REQUIRED) 
  
Name of the plugin executable/script. Usually it's a good idea to give
the file a name resembling the plugin ID.

### title (RECOMMENDED)
  
Short title of the Plugin. If omitted the 'id' field will be used
instead which might look a bit bad due to lowercasing, etc

### showtimeVersion (RECOMMENDED)

Minimum version required of Showtime for this plugin to work. If the
current version of Showtime is less than this version the user won't
be able to install the plugin but will be notified about what version
of Showtime is required. The same goes if a plugin is updated and the
new version requires a newer version of Showtime. Then the user will
be refused to upgrade the plugin.  If this field is omitted Showtime
will assume the plugin works on all versions of Showtime.

### version (RECOMMENDED)

Version of the plugin. If this does not match the current installed
version of a user's plugin the user will be presented with the
possibility to upgrade the plugin.  If the field is omitted Showtime
will set the version to "Unknown"

### synopsis (RECOMMENDED)

A short one line summary of the plugin or the service it accesses

### author (OPTIONAL)

Plugin developer. Any UTF-8 characters is valid.

### icon (OPTIONAL)

Path to plugin icon. The path is relative to the plugin root
directory. If no icon is available Showtime will use a placeholder
image instead.

### description (OPTIONAL)

Long rich-text formatted description of the plugin.



# Developing a plugin

To develop a plugin for Showtime it's easiest to load the plugin
via Showtime's plugin development argument (-p):

Example:

    build.linux/showtime -p testplugin

This will output something like:

    plugins [ERROR]: Unable to load development plugin: testplugin
                     Unable to load testplugin/plugin.json -- File not found

Go ahead and create the directory:

    mkdir testplugin

Edit the JSON file with your favorite text editor:

    emacs testplugin/plugin.json

And put the required fields in there:

    {
     "type": "javascript",
     "file": "testplugin.js",
     "id": "testplugin"
    }

You can reload the development plugin at any time by pressing Shift+F5
in Showtime (assuming you have not mapped that key/action to something
else in the keymapper setup). This works fine even if the plugin
failed to load for whatever reason. All hooks and resources that are
registered by the plugin is automatically removed when the plugin is
reloaded.  The same procedure happens when a plugin is uninstalled
from the plugin repository browser.  Pressing Shift+F5 will also
reload the data model for the current loaded page. Ir. If you have a
page opened that links to your plugin in development mode Showtime
will first reload the plugin and then reload the page itself. This
makes it very easy to do rapid development of plugins.

Now go ahead and edit the javascript file

    emacs testplugin/testplugin.js

Put one single line in that file:

    showtime.print('hello world');

Press Shift+F5 and the console window from where you started showtime
should say:

    hello world
    plugins [INFO]: Reloaded dev plugin testplugin/

Showtime will just execute the plugin which only outputs 'Hello world'
on the console and then exits. So, how to create something more
interesting?  First the javascript code needs to create a scope where
its local variables will live and also need to remember 'this' which
is, when the script is invoked, a plugin object created by
Showtime. Change the code to:

    (function(plugin) {
     showtime.print("Hello! I'm a plugin running inside Showtime " + 
                    showtime.currentVersionString);
    })(this);

## Routing an URI to the plugin

Almost everything in Showtime has an URI. This is also one of the way
for a user to interact with plugins.  Each plugin reserves URI space
starting with the plugin's ID. This is not imposed by any code in
Showtime but rather a convention. URI routes registered by plugins
take precedence over the URI routes that Showtime itself handle so it
is possible to, for example, add routes for certain well known domains
on the Internet (think http://www.youtube.com/ .. )

Back to our test code:

    (function(plugin) {
     // Enable URI routing for this plugin.
     plugin.config.URIRouting = true;
    
     // Add a URI route and a corresponding function to be invoked
     plugin.addURI("testplugin:hello", function(page) {
       showtime.print("I was called");
     })
    })(this);

Now, reload the plugin (Shift+F5) and type "testplugin:hello" in the
search input field on Showtime's home page. (Maybe you didn't know, but
the search field can be used to open any valid Showtime URI). Showtime
will open a new page with nothing but a spinning throbber and just
print the console message. Not very exciting but it's a good start.




# API reference

## The page object

### Properties

#### type

* directory
* item
* video

#### contents

If type is 'directory' this can be used to hint the UI what type of
items is being displayed so the UI can chose a way to layout the
items.

#### loading

Set to true if the page is loading. This is the default value.  When
set to true the user interface will display a throbber or similar
animation telling the user that loading is in progress. Should be set
to false by the plugin once the page have been filled with data.

#### source

Source URL for video content. Only relevant if page.type is set to "video"

#### metadata


#### entries

Number of total entries on the page when all item have been inserted.
Only used for user presentation purposes.


#### paginator

If a function is assigned to this property, that function will be
invoked when Showtime wants more items appended to the data model for,
so called, paginated loading. If this property is not set Showtime
will assume that the plugin populates the entire model during the
initial page load.


### Functions

#### appendItem(String URI, [String type], [Object metadata])

Append an item to the page. This should be used to populate data when
page.type == 'directory'

* URI - URI to be opened when the item is activated (clicked)

* type - Type of the item

> * directory - Directory that can be browsed
> * file - Any type of file or unknown file format
> * video - Video file
> * audio - Audio track
> * image - Image that can be displayed
> * album - Collection of audio tracks

* metadata - Additional information about the item


#### appendPassiveItem(String type, [Variable data], [Object metadata])

Append a, so called, passive item to the page. This should be used to
populate data when page.type == 'item'. These items are only used for
informational purposes and do not necessarily have URIs associated
with them.


#### appendAction(String type, String data, [Boolean enabled], [Object metadata])

#### error(String errorMessage)

A helper to switch the page in error mode and display the supplied
error message.

#### onEvent(String event, Function handler)


#### dump()

Cause Showtime to print a dump of the property tree representing the
page to the console.





## The plugin object

When a plugin is loaded it will be so with an associated plugin
object. The object can not be altered from the script (IE, no new
properties can be defined on the object).


### Properties

#### url

URL to the plugin's javascript file

#### path

Path to the plugin root. This should be used to form fully qualified
URLs to plugin resources (icons, etc)

#### URIRouting

If set to 'true' the plugin will participate in URI routing. IE. the
handlers registered via plugin.addURI() will be invoked when a URI is
opened that matches the registered regexp. The default value is
'true'. This value can be modified by the plugin itself (Usually via a
setting)


#### search

If set to 'true' the plugin will participate in search
requests. IE. the handlers registered via plugin.addSearcher() will be
invoked on search requests. The default value is 'true'. This value
can be modified by the plugin itself (Usually via a setting)


### Functions

#### addURI(String Regexp, Function Handler)

Add a function for handling a URI matched by a regexp. This is a
central function for adding browse pages that a user can navigate to.

* Regexp - Regular expression matching the URI
* Handler - Function to be invoked when the URI is opened

The Handler is called with one or more arguments. The first argument
is always the page object. The second and further arguments are
sub-strings as matched by the regexp

Example:


    plugin.addURI("testplugin:foo:(.*)", function(page, arg) {
     showtime.print("The argument is " + arg);
    })


If the testplugin:foo:bar URI is opened in Showtime this will print:
`The argument is bar` in the console


#### addSearcher(String Title, String Icon, Function Handler)



#### addHTTPAuth(String Regexp, Function Handler)

Add a handler for authenticating HTTP requests.

Plugins may add handlers for dealing with authentications for various
URLs.  This is typically used for OAuth based streaming services and
such. Even though the browsing and searching of content is handled in
the plugin the actual streaming of Audio / Video never passes thru the
plugin itself. To make it possible to authenticate requests which are
not touched by the plugins (and to ease authentication of requests from
within the plugin itself) the plugin can register HTTPAuth handlers.

* Regexp - Regular expression matching the URIs for which to handle
  authentication.
* Handler - Function invoked when requests needs to be authenticated.

The handler is invoked with one argument 'A authreq object' and the
plugin should return true to indicate that the authentication was
successful, false otherwise.

The authreq object have these methods:

* oauthToken(String ConsumerKey, String ConsumerSecret, String UserToken, String UserSecret)
  If you need to use OAuth 1.0 you know what those are for and how to get them

* rawAuth(String data)
  Just send raw data as the HTTP Authentication Header.
  This can be used for a variety of things, including OAuth 2.0




#### createService(String Title, String URL, String Type, Boolean Enabled, [String Icon])

* Title - Display name of the service
* Type - Service type, can be used for grouping the service.
  This should be any of:
> * video
> * audio
> * tv
> * image
> * other

* Enabled - Boolean if the service should be enabled or disabled.
  This can later be changed by modifying the "enabled" property of the
  returned object

Create a service (that will appear on the home screen) and return an
Object representing the service.

Returns a service object.

The created service is resource-tracked and will be removed when the
plugin is unloaded (or reloaded)

#### createSettings(String Title, [String IconURL], [String Description])

* Title - Display name of the service
* Icon - URL Path to icon
* Description - Long description of the settings

Create a setting group (that will appear in the settings page) and
return a settings object.

The settings object is not resource tracked and explicitly destroyed
like most other plugin related objects. Rather it is finalized when
the Javascript garbage collector will destroy it.

#### getAuthCredentials(String Source, String Reason, Boolean QueryUser, [String ID], [Boolean ForceTemporary])

Ask the user for authentication credentials.

* Source - Display name of the service/entity requiring
  authentication.  This should be rather short but should still
  identify the service uniquely. IE, it should inform the user from
  _what_ the popup originates.
* Reason - Reason for asking for authentication credentials. This can
  be things like "Service requires authentication" or in case the user
  tries a username/password but it fails, it should be something like:
  "Invalid Password". IE. it should inform the user _why_ the request
  popped up.
* QueryUser - If set to true the user will be queried for input. If
  this is false Showtime will only look in the keyring for previously
  entered credentials.
* ID - Extra ID for storing the credentials in the keyring. This will
  be appended to the plugin's own ID. If you only have one
  authentication point to worry about you don't need to supply this.
* ForceTemporary - Do not allow Showtime to store the credentials on
  disk. This also removes the 'remember me' option from the popup.
  Some online services, when using token based authentication, allows
  Showtime to get a token that can be renewed to retain authentication
  against the service. For these type of services it makes no sense to
  store the Username/Password in clear text.
  
## The settings object





## The service object

Service objects are bound to the plugin from which they were
created. If the plugin is unloaded, all service objects will be
destroyed.

### Properties

#### enabled

Boolean value that can be modified from the plugin to enable/disable
the service on the home screen

### Functions

#### destroy()

Will destroy the service. Note that if the Javascript garbage
collector destroys the object the service within showtime will not be
removed. The plugin must invoke the destroy() method to remove the
service or the user must unload the plugin to remove the service.
Therefore if the plugin have no setting for removing the service from
the home screen the service can just be created and the returned
object can be forgotten about.








## The showtime object

### Properties

#### currentVersionInt

Current version of Showtime encoded as an integer. This can be used to
check if running a given version X or greater, etc.

The version number is encoded as:

    v = major * 10000000 +  minor * 100000 + commit;

So to check if the running Showtime is 3.4.2 or greater, just do

    if(showtime.currentVersionInt >= 30400002) {
      // yep
    }

#### currentVersionString

Display name of current version. This will also include GIT commit hash
and dirty flag,

### Functions

#### trace(String Message)

Send Message to Showtime's log/trace engine on debug level.
This will appear on the console if Showtime is started with the -d option

#### print(String Message)

Print Message to the console.

#### httpGet(String URL, [Object QueryArgs])

* URL - URL to request. HTTP:// and HTTPS:// is supported.
* QueryArgs - Object with properties that will be appended to the URL
  as query arguments. This can also be a list of objects and if so the
  properties from each object will be merged.

Return a HTTP response object

#### httpPost(String URL, Object PostData, [Object QueryArgs])

* URL - URL to request. HTTP:// and HTTPS:// is supported.
* PostData - Object where each property will be encoded and POSTed as
  application/x-www-form-urlencoded content.
* QueryArgs - Object with properties that will be appended to the URL
  as query arguments. This can also be a list of objects and if so the
  properties from each object will be merged.

Return a HTTP response object

#### readFile(String URL)

Open the file specified by URL and return the contents as a String

#### querySplitString(String s)

Returns object with properties set to values according to contents
from the query string.

#### pathEscape(String s)

Escapes a URL Path component.

#### paramEscape(String s)

Escapes a HTTP query parameter component.

#### canHandle(String URL)

  Return true if Showtime can handle the URL. This does not open or probe
  the URL in any way. It's a strict syntactic check.

#### message(String Message, Boolean ShowOk, Boolean ShowCancel)

Display a message to the user.

* Message - text to display to the user
* ShowOk - Set to true if a OK button should be displayed
* ShowCancel - Set to true if a Cancel button should be displayed

Return true if OK was pressed, false otherwise.

#### sleep(Integer seconds)

Sleep for the specified time (in seconds).

#### JSONEncode(Object o)

JSON encodes the Object and return the encoded JSON string.

#### JSONDecode(String s)

JSON decodes the String and return a JSON Object.

#### time()

Return current time in seconds since 1970

#### durationToString(Integer duration)

Convert duration into a string that is suitable for display to the user
for presentation of duration (of movies, etc)

  43 -> 0:43
  3601 -> 1:00:01

## HTTP response object

### Properties

#### headers

Object with properties representing all of the received HTTP headers

### Functions

#### toString()

Convert the response to a String. This will do a number of things.

1. Convert the contents to UTF-8 (IE, convert from the charset set in
   the HTTP content-type response header. If no content-type header is
   present it assumes UTF-8.
   
2. If content-type is XML (application/xml or text/xml) it will strip
   the leading `<?xml/>` tag. This is so the string can be fed directly
   into Javascript's XML E4X parser.

