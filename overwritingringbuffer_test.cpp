/*
 * Part of permashift, a plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE OverwritingBuffer

#include "overwritingringbuffer.h"

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(WriteOverEdge)
{
	cOverwritingRingBuffer buffer(10);

	for (uchar i = 1; i < 12; i += 3)
	{
		uchar miniBuffer[] = { i, (uchar)(i + 1), (uchar)(i + 2) };
		buffer.WriteData(miniBuffer, 3);
	}

	uchar* data;
	uchar count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 8);
	uchar index = 0;
	for (uchar i = 3; i <= 10; i++, index++)
	{
		BOOST_CHECK_EQUAL(data[index], i);
	}

	count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 2);
	index = 0;
	for (uchar i = 11; i <= 12; i++, index++)
	{
		BOOST_CHECK_EQUAL(data[index], i);
	}

	count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 0);
}


BOOST_AUTO_TEST_CASE(SkipToEdge)
{
	cOverwritingRingBuffer buffer(10);

	for (uchar i = 1; i <= 15; i += 3)
	{
		uchar miniBuffer[] = { i, (uchar)(i + 1), (uchar)(i + 2) };
		buffer.WriteData(miniBuffer, 3);
	}

	buffer.DropData(5);

	uchar* data;
	uchar count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 5);
	int index = 0;
	for (uchar i = 11; i <= 15; i++, index++)
	{
		BOOST_CHECK_EQUAL(data[index], i);
	}

	count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 0);
}


BOOST_AUTO_TEST_CASE(SkipOverEdge)
{
	cOverwritingRingBuffer buffer(10);

	for (uchar i = 1; i <= 15; i += 3)
	{
		uchar miniBuffer[] = { i, (uchar)(i + 1), (uchar)(i + 2) };
		buffer.WriteData(miniBuffer, 3);
	}

	buffer.DropData(7);

	uchar* data;
	uchar count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 3);
	int index = 0;
	for (uchar i = 13; i <= 15; i++, index++)
	{
		BOOST_CHECK_EQUAL(data[index], i);
	}

	count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 0);
}



BOOST_AUTO_TEST_CASE(ReadFromEnd)
{
	cOverwritingRingBuffer buffer(10);

	for (uchar i = 1; i < 12; i += 3)
	{
		uchar miniBuffer[] = { i, (uchar)(i + 1), (uchar)(i + 2) };
		buffer.WriteData(miniBuffer, 3);
	}

	uchar* data;
	uchar count = buffer.ReadDataFromEnd(&data, 5);
	BOOST_CHECK_EQUAL(count, 2);
	uchar index = 0;
	for (uchar i = 11; i <= 12; i++, index++)
	{
		BOOST_CHECK_EQUAL(data[index], i);
	}

	count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 8);
	index = 0;
	for (uchar i = 3; i <= 10; i++, index++)
	{
		BOOST_CHECK_EQUAL(data[index], i);
	}

	count = buffer.ReadData(&data, 10);
	BOOST_CHECK_EQUAL(count, 0);
}

