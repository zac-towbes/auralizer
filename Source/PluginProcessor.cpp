/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"


//==============================================================================


AuralizerAudioProcessor::~AuralizerAudioProcessor()
{
    // lock processing before destruction, just in case
    processlock = true;
}

//==============================================================================
const String AuralizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AuralizerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AuralizerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AuralizerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AuralizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AuralizerAudioProcessor::getNumPrograms()
{
    return 1;
}

int AuralizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AuralizerAudioProcessor::setCurrentProgram (int index)
{
}

const String AuralizerAudioProcessor::getProgramName (int index)
{
    return {};
}

void AuralizerAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void AuralizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int _block_size = samplesPerBlock; //static_cast<int>(samplesPerBlock);
    const int _sample_rate = sampleRate;



    mono_block.setSize(1, _block_size);
    Ambi_block.setSize(getOrder[AMBISONIC_ORDER_NUMBER], samplesPerBlock);
//    Current_conv_block.setSize(1, _block_size);
//    convBlock_ptr = Current_conv_block.getReadPointer(0);
//    memset(&Current_conv_block, 0.0f, 2048);
//    memset(&Current_mono_block, 0.0f, 2048);
    

    Current_conv_block.fill(0.0f);
    Current_mono_block.fill(0.0f);
    Processed_conv_block.fill(0.0f);

#ifndef ENCODER_DIST
    Encoder.Configure(AMBISONIC_ORDER_NUMBER, true, 0);
#endif
#ifdef ENCODER_DIST
    EncoderDist.Configure(AMBISONIC_ORDER_NUMBER, TRUE, sampleRate);
    EncoderDist.Reset();
    EncoderDist.SetPosition(position);
#endif

    Decoder.Configure(AMBISONIC_ORDER_NUMBER, true, kAmblib_Stereo);

    position.fAzimuth = *yawAmt;
    position.fDistance = *distAmt;
    position.fElevation = *pitchAmt;


    Processor.Configure(AMBISONIC_ORDER_NUMBER, true, _block_size, 0);

    ambi_buffer.Configure(AMBISONIC_ORDER_NUMBER, true, _block_size);

    
}



void AuralizerAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.

    // lock processing during release...
    processlock = true;

    // when playback stops, set all buffer values to zero (other than IR buffers)
    ambi_buffer.Reset();
    Ambi_block.clear();
    mono_block.clear();

//
//    Current_conv_block.fill(0.0f);
//    Processed_conv_block.fill(0.0f);
//    Current_mono_block.fill(0.0f);

}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AuralizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return true;
//  #if JucePlugin_IsMidiEffect
//    ignoreUnused (layouts);
//    return true;
//  #else
//    // This is the place where you check if the layout is supported.
//    // In this template code we only support mono or stereo.
//    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
//     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
//        return false;
//
//    // This checks if the input layout matches the output layout
//   #if ! JucePlugin_IsSynth
//    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
//        return false;
//   #endif
//
//    return true;
//  #endif
}
#endif

void AuralizerAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    ScopedNoDenormals noDenormals;
    int totalNumInputChannels  = getTotalNumInputChannels();
    int totalNumOutputChannels = getTotalNumOutputChannels();
    int blocksize = buffer.getNumSamples();


    assert(totalNumInputChannels == 2 && totalNumOutputChannels == 2); // this is the stereo to stereo processor.

    if (! processlock){
        // sum the input channels into mono_block.
        for (int i = 0; i < blocksize; i++){
            Current_mono_block[i] = (buffer.getSample(0, i) + buffer.getSample(1, i)) * *inAmt;
        }

        // processes the mono input buffer through the ambisonic encoder.
        // note: mono_block also functions as the dry buffer, but it cannot in the evenutal stereo-stereo version.

#ifndef ENCODER_DIST
        Encoder.Process(Current_mono_block, blocksize, &ambi_buffer); //
#endif
#ifdef ENCODER_DIST
        EncoderDist.Process(Current_mono_block.data(), blocksize, &ambi_buffer);
#endif

        Processor.Process(&ambi_buffer, blocksize);
        // a loop through each channel of the ambisonic buffer
        for (int current_ambi_channel = 0; current_ambi_channel < getOrder[AMBISONIC_ORDER_NUMBER]; current_ambi_channel++){



            // copies the ambisonic buffer's current channel to the Current_conv_block.
            ambi_buffer.ExtractStream(Current_conv_block.data(), current_ambi_channel, blocksize);


            if (! processlock){
            // convolves with the current channel...
            Convolvers[current_ambi_channel].process(Current_conv_block.data(), Processed_conv_block.data(), (size_t) blocksize);

            // puts the convolved stream back into the ambisonic buffer...
            ambi_buffer.InsertStream(Processed_conv_block.data(), current_ambi_channel, blocksize);
            }
            else{
                ambi_buffer.InsertStream(Current_conv_block.data(), current_ambi_channel, blocksize);
            }
        }

        // decodes the ambisonic buffer into the array of write pointers.
        Decoder.Process(&ambi_buffer, blocksize, buffer.getArrayOfWritePointers());


        // the buffer now acts as the wet signal
        buffer.applyGain(*wetAmt);

        // and mono_block acts as the dry.
    //    mono_block.applyGain(*dryAmt);
        for (int sampleNum = 0; sampleNum < blocksize; sampleNum++){
            Current_mono_block[sampleNum] *= *dryAmt;
            buffer.setSample(0, sampleNum, ((Current_mono_block[sampleNum] * *dryAmt) + buffer.getSample(0, sampleNum)) * *outAmt);
            buffer.setSample(1, sampleNum, ((Current_mono_block[sampleNum] * *dryAmt) + buffer.getSample(1, sampleNum)) * *outAmt);
        }
        // add dry to wet. note: is this delay compensated? I guess we'll see. The docs on the convolver were confusing.
    //    buffer.addFrom(0, 0, mono_block, 0, 0, blocksize);
    //    buffer.addFrom(1, 0, mono_block, 0, 0, blocksize);

        // this should be it.
    }

}

//==============================================================================
bool AuralizerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

//bool AuralizerAudioProcessor::isBusesLayoutSupported( const AuralizerAudioProcessor::BusesLayout& ) const {
//    return true;
//}

AudioProcessorEditor* AuralizerAudioProcessor::createEditor()
{
    return new AuralizerAudioProcessorEditor (*this, parameters);
}

//==============================================================================
void AuralizerAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    processlock = true;
    auto state = parameters.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    copyXmlToBinary(*xml, destData);
    processlock = false;

}

void AuralizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    processlock = true;
    std::unique_ptr<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr){
        if (xmlState->hasTagName("auralizerParam_0")){
            new_preset_name = "default_presetSaveName";
            IR_directory = "~/Documents/auralizer/presets/";
            *wetAmt = (float) xmlState->getDoubleAttribute("wetAmt");
            *dryAmt = (float) xmlState->getDoubleAttribute("dryAmt");

            *inAmt = (float) xmlState->getDoubleAttribute("inAmt");
            *outAmt = (float) xmlState->getDoubleAttribute("outAmt");

            *yawAmt = (float) xmlState->getDoubleAttribute("yawAmt");
            *pitchAmt = (float) xmlState->getDoubleAttribute("pitchAmt");
            *rollAmt = (float) xmlState->getDoubleAttribute("rollAmt"); // ROLLTOGGLE
            *distAmt = (float) xmlState->getDoubleAttribute("distAmt"); // DISTTOGGLE

            *dirAmt = (float) xmlState->getDoubleAttribute("dirAmt");
            *earlyAmt = (float) xmlState->getDoubleAttribute("earlyAmt");
            *lateAmt = (float) xmlState->getDoubleAttribute("lateAmt");

            boost::filesystem::path IRDirPath(IR_directory.getCharPointer());

            loadIRs(IRDirPath);
            processlock = false;
        }
        else if (xmlState->hasTagName(parameters.state.getType())){
            parameters.replaceState (ValueTree::fromXml (*xmlState));
        }
    }
}

bool AuralizerAudioProcessor::loadPreset()
/*
 Loads the xml file representing a preset stored at xmlFileToLoad and sets the relevant settings.

 some of this functionality is likely a duplicate of setStateInformation() but I've done it separately here because there it's an xml binary and here it's an .xml file. Regardless, that could be changed easily.

 Returns true if the file is found and loaded, otherwise returns false.
 */
{
    processlock = true;
    std::unique_ptr<XmlElement> xmlState = XmlDocument(xmlFileToLoad).getDocumentElement();

    if (xmlState != nullptr){
//        if (xmlState->hasTagName("auralizerParam_0")){
            new_preset_name = xmlState->getStringAttribute("name");
            IR_directory = xmlState->getStringAttribute("IRDir");
            // note that IRDir may be relative... It will most likely be "./" if preset IRs are stored in the same directory as the presets themselves, which they generally will be for the moment.
            // Nonetheless, this is in place in case that changes.

            *wetAmt = (float) xmlState->getDoubleAttribute("wetAmt");
            *dryAmt = (float) xmlState->getDoubleAttribute("dryAmt");

            *inAmt = (float) xmlState->getDoubleAttribute("inAmt");
            *outAmt = (float) xmlState->getDoubleAttribute("outAmt");

            *yawAmt = (float) xmlState->getDoubleAttribute("yawAmt");
            *pitchAmt = (float) xmlState->getDoubleAttribute("pitchAmt");
            *rollAmt = (float) xmlState->getDoubleAttribute("rollAmt"); // ROLLTOGGLE
            *distAmt = (float) xmlState->getDoubleAttribute("distAmt"); // DISTTOGGLE

            *dirAmt = (float) xmlState->getDoubleAttribute("dirAmt");
            *earlyAmt = (float) xmlState->getDoubleAttribute("earlyAmt");
            *lateAmt = (float) xmlState->getDoubleAttribute("lateAmt");

            boost::filesystem::path IRDirPath(IR_directory.getCharPointer());
            loadIRs(IRDirPath);
            return true;
//        }
    }
    return false;
}



void AuralizerAudioProcessor::loadIRs(boost::filesystem::path IRDirPath){
    AudioFormat *audioFormat = formatManager.getDefaultFormat();
    
//    boost::filesystem::path dirIRPath  (boost::filesystem::current_path());
//    boost::filesystem::path earlyIRPath(boost::filesystem::current_path());
//    boost::filesystem::path lateIRPath (boost::filesystem::current_path());

    boost::filesystem::path dirIRPath (IRDirPath);
    boost::filesystem::path earlyIRPath (IRDirPath);
    boost::filesystem::path lateIRPath (IRDirPath);


    // append the IRDirPath to each IR path. note that /= is the append operator.
//    dirIRPath   /= IRDirPath;
//    earlyIRPath /= IRDirPath;
//    lateIRPath  /= IRDirPath;

    dirIRPath /= new_preset_name.toUTF8();
    earlyIRPath /= new_preset_name.toUTF8();
    lateIRPath /= new_preset_name.toUTF8();

    // append the correct filename to each path
    dirIRPath   /= "direct.wav";
    earlyIRPath /=  "early.wav";
    lateIRPath  /=   "late.wav";

    // instantiates a juce::File from the  boost::filesystem::path
    juce::File dirIRFile    (  dirIRPath.generic_string());
    juce::File earlyIRFile  (earlyIRPath.generic_string());
    juce::File lateIRFile   ( lateIRPath.generic_string());

    // creates juce::MemoryMappedAudioFormatReaders for each IR, maps them all. NOTE: this is probably relatively slow. Could we do this serially?
    if (dirIRFile.hasFileExtension(".wav") && earlyIRFile.hasFileExtension(".wav") && lateIRFile.hasFileExtension(".wav"))
    {
        MemoryMappedAudioFormatReader *dirReader = audioFormat->createMemoryMappedReader(dirIRFile);
        MemoryMappedAudioFormatReader *earlyReader = audioFormat->createMemoryMappedReader(earlyIRFile);
        MemoryMappedAudioFormatReader *lateReader = audioFormat->createMemoryMappedReader(lateIRFile);

        dirReader->mapEntireFile();
        earlyReader->mapEntireFile();
        lateReader->mapEntireFile();

        // clear each IR buffer
        dirIR.clear();
        earlyIR.clear();
        lateIR.clear();

        dirIR.setSize   (getOrder[AMBISONIC_ORDER_NUMBER], (int) dirReader->lengthInSamples   );
        earlyIR.setSize (getOrder[AMBISONIC_ORDER_NUMBER], (int) earlyReader->lengthInSamples );
        lateIR.setSize  (getOrder[AMBISONIC_ORDER_NUMBER], (int) lateReader->lengthInSamples  );

        ir_length = std::max(std::max(dirReader->lengthInSamples, earlyReader->lengthInSamples), lateReader->lengthInSamples);

        dirReader->read  (dirIR.getArrayOfWritePointers(),   getOrder[AMBISONIC_ORDER_NUMBER], 0, (int) dirReader->lengthInSamples  );
        earlyReader->read(earlyIR.getArrayOfWritePointers(), getOrder[AMBISONIC_ORDER_NUMBER], 0, (int) earlyReader->lengthInSamples);
        lateReader->read (lateIR.getArrayOfWritePointers(),  getOrder[AMBISONIC_ORDER_NUMBER], 0, (int) lateReader->lengthInSamples );

        updateIRs();
        audioFormat = nullptr;
    }
    else throw std::invalid_argument("Directory does not contain impulse response \".wav\" files.");
}

void AuralizerAudioProcessor::updateIRs(){

    // for each ambisonic channel
    processlock = true;
    for (int i = 0; i < getOrder[AMBISONIC_ORDER_NUMBER]; i++){
        // gets rid of the current contents of the full ir buffer and resets the size.
        fullIR[i].clear();
        fullIR[i].setSize(1, ir_length);

//        dirIR.setSize()
        fullIR_ptr[i] = fullIR[i].getReadPointer(0);

        // this adds each buffer's 0th channel to the full ir's 0th channel with the correct gain
        fullIR[i].addFromWithRamp  (0, 0, earlyIR.getReadPointer(i, 0), ir_length, *earlyAmt, *earlyAmt);
        fullIR[i].addFromWithRamp  (0, 0, lateIR.getReadPointer(i, 0),  ir_length, *lateAmt,  *lateAmt);
        fullIR[i].addFromWithRamp  (0, 0, dirIR.getReadPointer(i, 0),   ir_length, *dirAmt,   *dirAmt);

        // initializes the convolver with the IR
        ConvolverActive[i] = Convolvers[i].init(CONV_BLOCK_SIZE, fullIR_ptr[i], ir_length);
    }
    processlock = false;
}

void AuralizerAudioProcessor::modifyIRs(AudioBuffer<float> *IR_changed, float new_value, float previous_value, unsigned long length){
    int i = 0;
    int j = 0;

    for (i = 0; i < getOrder[AMBISONIC_ORDER_NUMBER]; i++){
        for (j = 0; j < length; j++){
            fullIR[i].setSample(0, j, fullIR[i].getSample(0, j) + (IR_changed->getSample(i, j) * (new_value - previous_value)));
        }
    }
}


void AuralizerAudioProcessor::setSliderValue(String name, float value){
    if (name == "wetSlider"){
        *wetAmt = value;
    }
    else if (name == "drySlider"){
        *dryAmt = value;
    }
    else if (name == "inSlider"){
        *inAmt = value;
    }
    else if (name == "outSlider"){
        *outAmt = value;
    }
    else if (name == "yawSlider"){
        *yawAmt = value;
    }
    else if (name == "pitchSlider"){
        *pitchAmt = value;
    }
    else if (name == "rollSlider"){ // ROLLTOGGLE
        *rollAmt = value; // ROLLTOGGLE
    } // ROLLTOGGLE
    else if (name == "distSlider"){ // DISTTOGGLE
        *distAmt = value; // DISTTOGGLE
        position.fDistance = value;
        EncoderDist.SetPosition(position);
    } // DISTTOGGLE
    else if (name == "directSlider"){
        previousValue = *dirAmt;
        *dirAmt = value;
        modifyIRs(&dirIR, value, previousValue, dirIR.getNumSamples());
    }
    else if (name == "earlySlider"){
        previousValue = *earlyAmt;
        *earlyAmt = value;
        modifyIRs(&earlyIR, value, previousValue, earlyIR.getNumSamples());
    }
    else if (name == "lateSlider"){
        previousValue = *lateAmt;
        *lateAmt = value;
        modifyIRs(&lateIR, value, previousValue, lateIR.getNumSamples());
    }
    else { // error
        throw std::invalid_argument("received invalid slider name argument");
    }

    // if any of the sliders that change the actual IR are changed
    if (name == "earlySlider" || name == "lateSlider" || name == "directSlider"){

    }
    if (name == "yawSlider" || name == "pitchSlider" || name == "rollSlider" || name == "distSlider"){
        Processor.SetOrientation(Orientation(*yawAmt, *pitchAmt, *rollAmt));
//        Processor.Refresh();
        Decoder.Refresh();
#ifndef ENCODER_DIST
        Encoder.Refresh();
#endif
#ifdef ENCODER_DIST
        EncoderDist.SetPosition(position);
        EncoderDist.Refresh();
#endif
//        Processor.SetOrientation(orientation);
    }

}


// returns the value of a slider so that PluginEditor may render sliders back to their correct positions.
// will soon be unnecessary with ValueTreeState attachments. 
float AuralizerAudioProcessor::getSliderValue(String name){
    if (name == "wetSlider") return *wetAmt;
    else if (name == "drySlider") return *dryAmt;
    else if (name == "inSlider") return *inAmt;
    else if (name == "outSlider") return *outAmt;
    else if (name == "yawSlider") return *yawAmt;
    else if (name == "pitchSlider") return *pitchAmt;
    else if (name == "rollSlider") return *rollAmt;
    else if (name == "distSlider") return *distAmt;
    else if (name == "directSlider") return *dirAmt;
    else if (name == "earlySlider") return *earlyAmt;
    else if (name == "lateSlider") return *lateAmt;
    else throw std::invalid_argument("received invalid slider name argument in getSliderValue");
}


//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AuralizerAudioProcessor();
}

