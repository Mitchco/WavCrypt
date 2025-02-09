/*
  ==============================================================================

    Encryptor.h
    Created: 11 Jun 2019 11:17:53pm
    Author:  Benedikt Sailer

 ==============================================================================

 Copyright 2019 Benedikt Sailer - mail@benediktsailer.com --- Idea by Daniel Walz - foleysfinest.com
 
 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ==============================================================================
 */


#pragma once

#define KeySize 8192
#define KeyChannels 2

#include "../JuceLibraryCode/JuceHeader.h"
//==============================================================================
class NoiseKeyGenerator : public juce::Thread
{
public:
    NoiseKeyGenerator()
    :
    Thread("NoiseKeyGeneratorThread")
    {
        afm.registerBasicFormats();
        
        random.setSeedRandomly();
    }
    
    void generateKey()
    {
        juce::FileChooser chooser("Choose key save location",
                    File::getCurrentWorkingDirectory(),
                    "*.wav",
                    false,
                    false,
                    nullptr);
        
        if (chooser.browseForFileToSave(true))
        {
            keyFile = chooser.getResult();
            startThread();
        }
    }
    
   
    
    void run() override
    {
        juce::AudioBuffer<float> noiseBuffer;
        noiseBuffer.setSize(numChannels, KeySize, false, true, false);
        
        for (auto ch = 0; ch < numChannels; ch++)
        {
            auto writePointer = noiseBuffer.getWritePointer(ch);
            
            for (auto s = 0; s < KeySize; s++)
            {
                writePointer[s] = jmap (random.nextFloat(), 0.0f, 1.0f, -1.0f, 1.0f);
            }
        }
        
        
        auto wavFormat = afm.getKnownFormat(0);
        
    
        if (!keyFile.exists())
            keyFile.create();
        
        auto* outStream = new FileOutputStream(keyFile);
        
        if (juce::ScopedPointer<AudioFormatWriter> audioWriter = wavFormat->createWriterFor(outStream, 48000, numChannels, 24, StringPairArray(), 0))
            
        {
            audioWriter->writeFromAudioSampleBuffer(noiseBuffer, 0, noiseBuffer.getNumSamples());
        }
        else jassertfalse; //error initializing audioWriter


        
    }
   
    
private:
    const int numChannels = KeyChannels;
   
    juce::Random random;
    juce::AudioFormatManager afm;
 
    juce::File keyFile;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NoiseKeyGenerator)
};

//==============================================================================
class WavEncryptor : public juce::Thread
{
public:
    WavEncryptor()
    :
    Thread("EncryptWavThread")
    {
        afm.registerBasicFormats();
        
       
    }
    
    void encryptWavFiles()
    {
        juce::FileChooser originalFolderChooser("Choose original wavs folder",
                                            File::getCurrentWorkingDirectory(),
                                            "",
                                            false,
                                            false,
                                            nullptr);
        
        if (originalFolderChooser.browseForDirectory())
        {
            originalFolder = originalFolderChooser.getResult();
            encryptedFolder = File(originalFolder.getParentDirectory().getFullPathName() + "/Encrypted");
            
            if (!encryptedFolder.exists())
                encryptedFolder.createDirectory();
            
        }
        
        juce::FileChooser keyFileChooser("Select Key File",
                                          File::getCurrentWorkingDirectory(),
                                          "*.wav",
                                          false,
                                          false,
                                          nullptr);
        
        if (keyFileChooser.browseForFileToOpen())
        {
            auto keyFile = keyFileChooser.getResult();
            
            
            if (juce::ScopedPointer<AudioFormatReader> reader = afm.createReaderFor(keyFile.createInputStream()))
            // if(juce::AudioFormatReader *reader = afm.createReaderFor( keyFile.createInputStream()))

            {
                keyBuffer.setSize(reader->numChannels, reader->lengthInSamples, false, true, false);
                reader->read(&keyBuffer, 0, reader->lengthInSamples, 0, true, reader->numChannels > 1);
                keyBufferLength = reader->lengthInSamples;
            }
            else jassertfalse; //reader error
            
             startThread();
        }
        
      
        
        
    }
    
    ~WavEncryptor()
    {
        signalThreadShouldExit();
        while (isThreadRunning())
        {
            
        }
    }
    
    void run() override
    {
        juce::DirectoryIterator iter (originalFolder, true, "*.wav", File::findFiles);
        
        auto wavFormat = afm.getKnownFormat(0);
        
        while (iter.next() && !threadShouldExit())
        {
          auto file = iter.getFile();
            
           if (juce::ScopedPointer<AudioFormatReader> reader = afm.createReaderFor(file))
           {
               jassert(reader->numChannels == keyBuffer.getNumChannels()); //Channel mismatch between key and file to encrypt
               
               juce::AudioBuffer<float> originalBuffer;
               originalBuffer.setSize(reader->numChannels, reader->lengthInSamples, false, true, false);
               
               reader->read(&originalBuffer, 0, reader->lengthInSamples, 0, true, reader->numChannels > 1);
               
              
               //add noise
               for (int ch = 0; ch < reader->numChannels; ch++)
               {
                   int keyBufferReadHead = 0;
                   auto writePointer = originalBuffer.getWritePointer(ch);
                   auto keyReadPointer = keyBuffer.getReadPointer(ch);
                   
                   for (int s = 0; s < reader->lengthInSamples; s++)
                   {
                       writePointer[s] += (keyReadPointer[keyBufferReadHead] * -0.3f);
                       
                       keyBufferReadHead = (keyBufferReadHead + 1) % keyBufferLength;
                   }
               }
               
               juce::File newFile (encryptedFolder.getFullPathName() + "/" + file.getFileNameWithoutExtension() + "_encrypted" + file.getFileExtension());
               auto* outStream = new FileOutputStream(newFile);
               if (ScopedPointer<AudioFormatWriter> writer = wavFormat->createWriterFor(outStream, reader->sampleRate, reader->numChannels, reader->bitsPerSample, StringPairArray(), 0) )
               {
                   writer->writeFromAudioSampleBuffer(originalBuffer, 0, originalBuffer.getNumSamples());
               }
               else jassertfalse; //AudioFormatWriter error
           }
           else jassertfalse; //AudioFormatReader error
            
            
          
            if (reportProgress) reportProgress(iter.getEstimatedProgress());
           
        }
        
         if (reportProgress) reportProgress(1.0f);
    }
    
    std::function<void(float)> reportProgress;
    
private:
    juce::File originalFolder;
    juce::File encryptedFolder;
    
    juce::AudioFormatManager afm;
    
    juce::AudioBuffer<float> keyBuffer;
    int keyBufferLength;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavEncryptor)
};

//==============================================================================
class WavDecryptor : public juce::Thread
{
public:
    WavDecryptor()
    :
    Thread("EncryptWavThread")
    {
        afm.registerBasicFormats();
    }
    
    ~WavDecryptor()
    {
        signalThreadShouldExit();
        while (isThreadRunning())
        {
            
        }
    }
    
    void decryptWavFiles()
    {
        juce::FileChooser encryptedFolderChooser("Choose encrypted wavs folder",
                                          File::getCurrentWorkingDirectory(),
                                          "",
                                          false,
                                          false,
                                          nullptr);
        
        if (encryptedFolderChooser.browseForDirectory())
        {
            encryptedFolder = encryptedFolderChooser.getResult();
            decryptedFolder = File(encryptedFolder.getParentDirectory().getFullPathName() + "/Decrypted");
            
            if (!decryptedFolder.exists())
                decryptedFolder.createDirectory();
            
        }
        
        juce::FileChooser keyFileChooser("Select Key File",
                                   File::getCurrentWorkingDirectory(),
                                   "*.wav",
                                   false,
                                   false,
                                   nullptr);
        
        if (keyFileChooser.browseForFileToOpen())
        {
            auto keyFile = keyFileChooser.getResult();
            
            if (juce::ScopedPointer<AudioFormatReader> reader = afm.createReaderFor(keyFile.createInputStream()))
            // if (auto reader = std::unique_ptr<AudioFormatReader>(afm.createReaderFor (keyFile.createInputStream())))
            {
                keyBuffer.setSize(reader->numChannels, reader->lengthInSamples, false, true, false);
                reader->read(&keyBuffer, 0, reader->lengthInSamples, 0, true, reader->numChannels > 1);
                keyBufferLength = reader->lengthInSamples;
            }
            else jassertfalse; //reader error
            
            startThread();
        }
        
        
        
        
    }
    
    void run() override
    {
        juce::DirectoryIterator iter (encryptedFolder, true, "*.wav", File::findFiles);
        
        auto wavFormat = afm.getKnownFormat(0);
        
        while (iter.next() && !threadShouldExit())
        {
            auto file = iter.getFile();
            
            if (juce::ScopedPointer<AudioFormatReader> reader = afm.createReaderFor(file))
            {
                jassert(reader->numChannels == keyBuffer.getNumChannels()); //Channel mismatch between key and file to encrypt
                
                juce::AudioBuffer<float> originalBuffer;
                originalBuffer.setSize(reader->numChannels, reader->lengthInSamples, false, true, false);
                
                reader->read(&originalBuffer, 0, reader->lengthInSamples, 0, true, reader->numChannels > 1);
                
                
                //add noise
                for (int ch = 0; ch < reader->numChannels; ch++)
                {
                    int keyBufferReadHead = 0;
                    auto writePointer = originalBuffer.getWritePointer(ch);
                    auto keyReadPointer = keyBuffer.getReadPointer(ch);
                    
                    for (int s = 0; s < reader->lengthInSamples; s++)
                    {
                        writePointer[s] += (keyReadPointer[keyBufferReadHead] * 0.3f);
                        
                        keyBufferReadHead = (keyBufferReadHead + 1) % keyBufferLength;
                    }
                }
                
                juce::File newFile (decryptedFolder.getFullPathName() + "/" + file.getFileNameWithoutExtension() + "_decrypted" + file.getFileExtension());
                auto* outStream = new FileOutputStream(newFile);
                if (juce::ScopedPointer<AudioFormatWriter> writer = wavFormat->createWriterFor(outStream, reader->sampleRate, reader->numChannels, reader->bitsPerSample, StringPairArray(), 0) )
                {
                    writer->writeFromAudioSampleBuffer(originalBuffer, 0, originalBuffer.getNumSamples());
                }
                else jassertfalse; //AudioFormatWriter error
            }
            else jassertfalse; //AudioFormatReader error
            
            
            
         
            if (reportProgress) reportProgress(iter.getEstimatedProgress());
           
        }
        if (reportProgress) reportProgress(1.0f);
    }
    
    std::function<void(float)> reportProgress;
    
private:
    juce::File encryptedFolder;
    juce::File decryptedFolder;
    
    juce::AudioFormatManager afm;
    
    juce::AudioBuffer<float> keyBuffer;
    int keyBufferLength;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavDecryptor)
};
