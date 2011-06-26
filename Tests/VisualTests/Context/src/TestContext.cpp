/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2009 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "TestContext.h"
#include "VisualTest.h"
#include "SamplePlugin.h"
#include "TestResults.h"

#include "OgrePlatform.h"
#if OGRE_PLATFORM == OGRE_PLATFORM_SYMBIAN
#include <coecntrl.h>
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#endif

TestContext::TestContext() :mTimestep(0.01f), mBatch(0) {}
//-----------------------------------------------------------------------

TestContext::~TestContext()
{
    if(mBatch)
        delete mBatch;
}
//-----------------------------------------------------------------------

void TestContext::setup()
{
    // standard setup
    SampleContext::setup();

    // get the path and list of test plugins from the config file
    Ogre::ConfigFile testConfig;
    testConfig.load("tests.cfg");
    mPluginDirectory = testConfig.getSetting("TestFolder");
    mTestPlugins = testConfig.getMultiSetting("TestPlugin");

    // Eventually there will be a brief menu screen for selecting options,
    // for now, just hardcode this here and jump into a batch of tests immediatly:
    Ogre::String plugin = "VTests";
    
    // name of the plugin we'll be running
    mPluginName = plugin;

    // timestamp for the filename
    char temp[19];
    time_t raw = time(0);
    strftime(temp, 19, "%Y_%m_%d_%H%M_%S", gmtime(&raw));
    Ogre::String filestamp = Ogre::String(temp, 18);
	// name for this batch (used for naming the directory, and uniquely identifying this batch)
    Ogre::String batchName = mPluginName + "_" + filestamp;
    
    // a nicer formatted version for display
    strftime(temp, 20, "%Y-%m-%d %H:%M:%S", gmtime(&raw));
    Ogre::String timestamp = Ogre::String(temp, 19);
    
    // set up output directories
    setupDirectories(batchName);

    // an object storing info about this set
    mBatch = new TestBatch(batchName, mPluginName, timestamp, 
        mWindow->getWidth(), mWindow->getHeight(), mOutputDir + batchName + "/");

    OgreBites::Sample* firstTest = loadTests(mPluginName);
    runSample(firstTest);
}
//-----------------------------------------------------------------------

OgreBites::Sample* TestContext::loadTests(Ogre::String plugin)
{
    OgreBites::Sample* startSample = 0;
    Ogre::StringVector sampleList;

    // try loading up the test plugin
    try
    {
        mRoot->loadPlugin(mPluginDirectory + "/" + plugin);
    }
    // if it fails, just return right away
    catch (Ogre::Exception e)
    {
        return 0;
    }

    // grab the plugin and cast to SamplePlugin
    Ogre::Plugin* p = mRoot->getInstalledPlugins().back();
    OgreBites::SamplePlugin* sp = dynamic_cast<OgreBites::SamplePlugin*>(p);

    // if something's gone wrong return null
    if (!sp)
        return 0;
    
    // go through every sample (test) in the plugin...
    OgreBites::SampleSet newSamples = sp->getSamples();
    for (OgreBites::SampleSet::iterator j = newSamples.begin(); j != newSamples.end(); j++)
    {
        mTests.push_back(*j);
        Ogre::NameValuePairList& info = (*j)->getInfo();   // acquire custom sample info
        Ogre::NameValuePairList::iterator k;

        // give sample default title and category if none found
        k= info.find("Title");
        if (k == info.end() || k->second.empty()) info["Title"] = "Untitled";
        k = info.find("Category");
        if (k == info.end() || k->second.empty()) info["Category"] = "Unsorted";
        k = info.find("Thumbnail");
        if (k == info.end() || k->second.empty()) info["Thumbnail"] = "thumb_error.png";
    }

    startSample = *newSamples.begin();

    return startSample;
}
//-----------------------------------------------------------------------

bool TestContext::frameStarted(const Ogre::FrameEvent& evt)
{
    captureInputDevices();

    // pass a fixed timestep along to the tests
    Ogre::FrameEvent fixed_evt = Ogre::FrameEvent();
    fixed_evt.timeSinceLastFrame = mTimestep;
    fixed_evt.timeSinceLastEvent = mTimestep;

    if(mCurrentTest) // if a test is running
    {
        // track frame number for screenshot purposes
        ++mCurrentFrame;

        // regular update function
        return mCurrentTest->frameStarted(fixed_evt);
    }
    else if(mCurrentSample) // if a generic sample is running
    {
        return mCurrentSample->frameStarted(evt);
    }
    else
    {
        // if no more tests are queued, generate output and exit
        finishedTests();
        return false;
    }
}
//-----------------------------------------------------------------------

bool TestContext::frameEnded(const Ogre::FrameEvent& evt)
{
    // pass a fixed timestep along to the tests
    Ogre::FrameEvent fixed_evt = Ogre::FrameEvent();
    fixed_evt.timeSinceLastFrame = mTimestep;
    fixed_evt.timeSinceLastEvent = mTimestep;

    if(mCurrentTest) // if a test is running
    {
        if(mCurrentTest->isScreenshotFrame(mCurrentFrame))
        {
            // take a screenshot
            Ogre::String filename = mOutputDir + mBatch->name + "/" +
                mCurrentTest->getInfo()["Title"] + "_" + 
                Ogre::StringConverter::toString(mCurrentFrame) + ".png";
            // remember the name of the shot, for later comparison purposes
            mBatch->images.push_back(mCurrentTest->getInfo()["Title"] + "_" + 
                Ogre::StringConverter::toString(mCurrentFrame));
            mWindow->writeContentsToFile(filename);
        }

        if(mCurrentTest->isDone())
        {
            // continue onto the next test
            runSample(0);
            return true;
        }

        // standard update function
        return mCurrentTest->frameEnded(fixed_evt);
    }
    else if(mCurrentSample) // if a generic sample is running
    {
        return mCurrentSample->frameEnded(evt);
    }
    else
    {
        // if no more tests are queued, generate output and exit
        finishedTests();
        return false;
    }
}
//-----------------------------------------------------------------------

void TestContext::runSample(OgreBites::Sample* s)
{
    // reset frame timing
    Ogre::ControllerManager::getSingleton().setFrameDelay(0);
    Ogre::ControllerManager::getSingleton().setTimeFactor(1.f);
    mCurrentFrame = 0;

    OgreBites::Sample* sampleToRun = s;

    // if a valid test is passed, then run it, if null, grab the next one from the deque
    if(s)
    {
        sampleToRun = s;
    }
    else if(!mTests.empty())
    {
        mTests.pop_front();
        if(!mTests.empty())
            sampleToRun = mTests.front();
    }

    // check if this is a VisualTest
    mCurrentTest = dynamic_cast<VisualTest*>(sampleToRun);

    // set things up to be deterministic
    if(mCurrentTest)
    {
       // Seed rand with a predictable value
        srand(5); // 5 is completely arbitrary, the idea is simply to use a constant

        // Give a fixed timestep for particles and other time-dependent things in OGRE
        Ogre::ControllerManager::getSingleton().setFrameDelay(mTimestep);
    }

    SampleContext::runSample(sampleToRun);
}
//-----------------------------------------------------------------------

void TestContext::createRoot()
{
// note that we use a separate config file here
#if OGRE_PLATFORM == OGRE_PLATFORM_ANDROID
    mRoot = Ogre::Root::getSingletonPtr();
#else
    Ogre::String pluginsPath = Ogre::StringUtil::BLANK;
    #ifndef OGRE_STATIC_LIB
        pluginsPath = mFSLayer->getConfigFilePath("plugins.cfg");
    #endif
    // we use separate config and log files for the tests
    mRoot = OGRE_NEW Ogre::Root(pluginsPath, mFSLayer->getWritablePath("ogretests.cfg"), 
        mFSLayer->getWritablePath("ogretests.log"));
#endif

#ifdef OGRE_STATIC_LIB
    mStaticPluginLoader.load();
#endif
}
//-----------------------------------------------------------------------

void TestContext::setupDirectories(Ogre::String batchName)
{
    // ensure there's a root directory for visual tests
    mOutputDir = mFSLayer->getWritablePath("VisualTests/");
    static_cast<OgreBites::FileSystemLayerImpl*>(mFSLayer)->createDirectory(mOutputDir);
    
    // make sure there's a directory for the test plugin
    mOutputDir += mPluginName + "/";
    static_cast<OgreBites::FileSystemLayerImpl*>(mFSLayer)->createDirectory(mOutputDir);
    
    // add a directory for the render system
    Ogre::String rsysName = Ogre::Root::getSingleton().getRenderSystem()->getName();
    // strip spaces from render system name
    for(int i = 0;i < rsysName.size(); ++i)
        if(rsysName[i] != ' ')
            mOutputDir += rsysName[i];
    mOutputDir += "/";
    static_cast<OgreBites::FileSystemLayerImpl*>(mFSLayer)->createDirectory(mOutputDir);

    // and finally a directory for the test batch itself
    static_cast<OgreBites::FileSystemLayerImpl*>(mFSLayer)->createDirectory(mOutputDir
        + batchName + "/");
}
//-----------------------------------------------------------------------

void TestContext::finishedTests()
{
    // run comparison against the most recent test output (if one exists)
    TestBatchSetPtr batches = TestBatch::loadTestBatches(mOutputDir);
    TestBatchSet::iterator i = batches->begin();
    for(i; i != batches->end(); ++i)
    {
        if(mBatch->canCompareWith((*i)))
        {
            // we save a generally named "out.html" that gets overwritten each run, 
            // plus a uniquely named one for this run
            std::ofstream out1;
            std::ofstream out2;
            out1.open(Ogre::String(mOutputDir + "out.html").c_str());
            out2.open(Ogre::String(mOutputDir + "TestResults_" + mBatch->name + ".html").c_str());
            if(out1.is_open() && out2.is_open())
            {
                Ogre::String html = writeHTML(*i, *mBatch, mBatch->compare((*i)));
                out1<<html;
                out2<<html;
                out1.close();
                out2.close();
            }
            break;
        }
    }

    // write this batch's config file
    mBatch->writeConfig();
}
//-----------------------------------------------------------------------

Ogre::Real TestContext::getTimestep()
{
    return mTimestep;
}
//-----------------------------------------------------------------------

void TestContext::setTimestep(Ogre::Real timestep)
{
    // ensure we're getting a positive value
    mTimestep = timestep >= 0.f ? timestep : mTimestep;
}
//-----------------------------------------------------------------------

// main, platform-specific stuff is copied from SampleBrowser and not guaranteed to work...

#if OGRE_PLATFORM != OGRE_PLATFORM_SYMBIAN    

// TODO: setup CMake to use winmain rather than console
//#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
//INT WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, INT)
//#else
int main(int argc, char *argv[])
//#endif
{
#if OGRE_PLATFORM == OGRE_PLATFORM_APPLE_IOS
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    int retVal = UIApplicationMain(argc, argv, @"UIApplication", @"AppDelegate");
    [pool release];
    return retVal;
#elif (OGRE_PLATFORM == OGRE_PLATFORM_APPLE) && __LP64__
    NSAutoreleasePool * pool = [[NSAutoreleasePool alloc] init];
    
    mAppDelegate = [[AppDelegate alloc] init];
    [[NSApplication sharedApplication] setDelegate:mAppDelegate];
    int retVal = NSApplicationMain(argc, (const char **) argv);

    [pool release];

    return retVal;
#else

    try
    {
        TestContext tc = TestContext();
        tc.go();
    }
    catch (Ogre::Exception& e)
    {
#if OGRE_PLATFORM == OGRE_PLATFORM_WIN32
        MessageBoxA(NULL, e.getFullDescription().c_str(), "An exception has occurred!", MB_ICONERROR | MB_TASKMODAL);
#else
        std::cerr << "An exception has occurred: " << e.getFullDescription().c_str() << std::endl;
#endif
    }

#endif
    return 0;
}

#endif // OGRE_PLATFORM != OGRE_PLATFORM_SYMBIAN    
