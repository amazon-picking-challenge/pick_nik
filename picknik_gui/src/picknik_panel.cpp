/*
 * Copyright (c) 2011, Willow Garage, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Willow Garage, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>

#include <QPainter>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>

#include <std_msgs/Bool.h>

#include "picknik_panel.h"

namespace picknik_gui
{

// BEGIN_TUTORIAL
// Here is the implementation of the TeleopPanel class.  TeleopPanel
// has these responsibilities:
//
// - Act as a container for GUI elements DriveWidget and QLineEdit.
// - Publish command velocities 10 times per second (whether 0 or not).
// - Saving and restoring internal state from a config file.
//
// We start with the constructor, doing the standard Qt thing of
// passing the optional *parent* argument on to the superclass
// constructor, and also zero-ing the velocities we will be
// publishing.
PickNikPanel::PickNikPanel( QWidget* parent )
  : rviz::Panel( parent )
{
  // Next we lay out the "output topic" text entry field using a
  // QLabel and a QLineEdit in a QHBoxLayout.
  // QHBoxLayout* topic_layout = new QHBoxLayout;
  // topic_layout->addWidget( new QLabel( "Output Topic:" ));
  // output_topic_editor_ = new QLineEdit;
  // topic_layout->addWidget( output_topic_editor_ );

  // Create a push button
  btn_next_ = new QPushButton(this);
  btn_next_->setText("Next Step");
  connect( btn_next_, SIGNAL( clicked() ), this, SLOT( moveNextStep() ) );

  // Create a push button
  btn_run_ = new QPushButton(this);
  btn_run_->setText("Continue");
  connect( btn_run_, SIGNAL( clicked() ), this, SLOT( moveRun() ) );
  
  // Buttons horizontal
  QHBoxLayout* hlayout = new QHBoxLayout;
  hlayout->addWidget( btn_next_ );
  hlayout->addWidget( btn_run_ );

  // Lay out the topic field above the control widget.
  QVBoxLayout* layout = new QVBoxLayout;
  //layout->addLayout( topic_layout );
  layout->addLayout( hlayout );
  setLayout( layout );

  // Create a timer for sending the output.  Motor controllers want to
  // be reassured frequently that they are doing the right thing, so
  // we keep re-sending velocities even when they aren't changing.
  // 
  // Here we take advantage of QObject's memory management behavior:
  // since "this" is passed to the new QTimer as its parent, the
  // QTimer is deleted by the QObject destructor when this PickNikPanel
  // object is destroyed.  Therefore we don't need to keep a pointer
  // to the timer.
  //QTimer* output_timer = new QTimer( this );

  // Next we make signal/slot connections.
  //connect( drive_widget_, SIGNAL( outputVelocity( float, float )), this, SLOT( setVel( float, float )));
  //connect( output_topic_editor_, SIGNAL( editingFinished() ), this, SLOT( updateTopic() ));
  //connect( output_timer, SIGNAL( timeout() ), this, SLOT( sendVel() ));

  // Start the timer.
  //output_timer->start( 100 );

  next_publisher_ = nh_.advertise<std_msgs::Bool>( "/picknik_main/next", 1 );
  run_publisher_ = nh_.advertise<std_msgs::Bool>( "/picknik_main/run", 1 );

  // Make the control widget start disabled, since we don't start with an output topic.
  btn_next_->setEnabled( true );
  btn_run_->setEnabled( true );
}

void PickNikPanel::moveNextStep()
{
  ROS_INFO_STREAM_NAMED("picknik","Move to next step");
  std_msgs::Bool result;
  result.data = true;
  next_publisher_.publish( result );
}

void PickNikPanel::moveRun()
{
  ROS_INFO_STREAM_NAMED("picknik","Running continously");
  std_msgs::Bool result;
  result.data = true;
  run_publisher_.publish( result );
}

// Read the topic name from the QLineEdit and call setTopic() with the
// results.  This is connected to QLineEdit::editingFinished() which
// fires when the user presses Enter or Tab or otherwise moves focus
// away.
// void PickNikPanel::updateTopic()
// {
//   setTopic( output_topic_editor_->text() );
// }

// Set the topic name we are publishing to.
// void PickNikPanel::setTopic( const QString& new_topic )
// {
//   // Only take action if the name has changed.
//   if( new_topic != output_topic_ )
//   {
//     output_topic_ = new_topic;
//     // If the topic is the empty string, don't publish anything.
//     if( output_topic_ == "" )
//     {
//       next_publisher_.shutdown();
//       run_publisher_.shutdown();
//     }
//     else
//     {
//       // The old ``next_publisher_`` is destroyed by this assignment,
//       // and thus the old topic advertisement is removed.  The call to
//       // nh_advertise() says we want to publish data on the new topic
//       // name.
//     }
//     // rviz::Panel defines the configChanged() signal.  Emitting it
//     // tells RViz that something in this panel has changed that will
//     // affect a saved config file.  Ultimately this signal can cause
//     // QWidget::setWindowModified(true) to be called on the top-level
//     // rviz::VisualizationFrame, which causes a little asterisk ("*")
//     // to show in the window's title bar indicating unsaved changes.
//     Q_EMIT configChanged();
//   }

//   // Gray out the control widget when the output topic is empty.
//   btn_next_->setEnabled( output_topic_ != "" );
//   btn_run_->setEnabled( output_topic_ != "" );
// }

// Save all configuration data from this panel to the given
// Config object.  It is important here that you call save()
// on the parent class so the class id and panel name get saved.
void PickNikPanel::save( rviz::Config config ) const
{
  rviz::Panel::save( config );
  //config.mapSetValue( "Topic", output_topic_ );
}

// Load all configuration data for this panel from the given Config object.
void PickNikPanel::load( const rviz::Config& config )
{
  rviz::Panel::load( config );
  // QString topic;
  // if( config.mapGetString( "Topic", &topic ))
  // {
  //   output_topic_editor_->setText( topic );
  //   updateTopic();
  // }
}

} // end namespace picknik_gui

// Tell pluginlib about this class.  Every class which should be
// loadable by pluginlib::ClassLoader must have these two lines
// compiled in its .cpp file, outside of any namespace scope.
#include <pluginlib/class_list_macros.h>
PLUGINLIB_EXPORT_CLASS(picknik_gui::PickNikPanel,rviz::Panel )
// END_TUTORIAL